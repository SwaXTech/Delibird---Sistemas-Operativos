/*
 * planificador.c
 *
 *  Created on: 1 may. 2020
 *      Author: utnso
 */

#include "planificador.h"

t_list* entrenadores_new; // lista de t_tcb*
t_list* entrenadores_ready; // lista de t_tcb*
t_list* entrenadores_blocked_idle; // lista de t_tcb* que se bloquea sin tareas
t_list* entrenadores_blocked_waiting_caught; // acá van los que se bloquean esperando a recibir un caught
t_list* entrenadores_blocked_full; // Bloqueados por no poder agarrar mas pokemones, pero no cumplen su objetivo
t_list* entrenadores_exit; // lista de t_tcb*
t_tcb* entrenador_exec;

t_list* objetivo_global;  // lista de t_inventario*

t_queue* pokemones_en_el_mapa; // Lista de t_pokemon_en_mapa*

int retardo_ciclo_cpu;

// VER DE USAR SEM_T para hacer contadores
sem_t counter_pokemones_libres; // para ver si hay pokemones que no están siendo buscados por algún entrenador
sem_t counter_entrenadores_disponibles; // para ver si hay entrenadores disponibles (new/blocked_idle)

//////////////////////////////////////
//				STRUCTS				//
//////////////////////////////////////

t_pokemon_en_mapa* crearPokemonEnMapa(t_pokemon* pokemon, t_coords* posicion) {
	t_pokemon_en_mapa* pokemon_en_mapa = malloc(sizeof(t_pokemon_en_mapa));
	pokemon_en_mapa->pokemon = pokemon;
	pokemon_en_mapa->posicion = posicion;

	return pokemon_en_mapa;
}

//////////////////////////////////////
//			INICIALIZACION			//
//////////////////////////////////////

void cargarEntrenadores(void) {
	entrenadores_new = list_create();
	objetivo_global = list_create();

	char** posiciones_entrenadores = config_get_array_value(config, "POSICIONES_ENTRENADORES");
	char** pokemon_entrenadores    = config_get_array_value(config, "POKEMON_ENTRENADORES");
	char** objetivos_entrenadores  = config_get_array_value(config, "OBJETIVOS_ENTRENADORES");

	t_tcb* tcb_nuevo;
	pthread_t thread;

	int i = 0;
	while(posiciones_entrenadores[i] != NULL) {

		// CADA i ES UN ENTRENADOR NUEVO

		tcb_nuevo = malloc(sizeof(t_tcb));
		tcb_nuevo->entrenador = malloc(sizeof(t_entrenador));

		tcb_nuevo->entrenador->id_entrenador = i + 1;

		tcb_nuevo->entrenador->posicion = crearCoordenadas(posiciones_entrenadores[i]);

		tcb_nuevo->entrenador->destino = NULL;
		tcb_nuevo->entrenador->pokes_actuales = crearListaDeInventario(pokemon_entrenadores[i], NULL);

		tcb_nuevo->entrenador->pokes_objetivos = crearListaDeInventario(objetivos_entrenadores[i], objetivo_global);

		pthread_mutex_init(&(tcb_nuevo->mutex_ejecucion), NULL); // TODO pthread_mutex_destroy cuando se deje de usar para siempre
		pthread_mutex_lock(&(tcb_nuevo->mutex_ejecucion)); // Lo bloqueo al principio asi no empieza la ejecucion del entrenador, se desbloquea cuando lo pase a ejecucion

		list_add(entrenadores_new, tcb_nuevo);



		pthread_create(&thread, NULL, entrenadorMain, tcb_nuevo);
		pthread_detach(thread);
		i++;
	}


	liberarListaDePunteros(posiciones_entrenadores);
	liberarListaDePunteros(pokemon_entrenadores);
	liberarListaDePunteros(objetivos_entrenadores);


	sem_init(&counter_entrenadores_disponibles, 0, i); // i es la cantidad de entrenadores cargados, que siempre seria la misma.
}

void enviarGetsAlBroker(void) {
	t_inventario* objetivo;
	pthread_t thread;
	for(int i = 0; i < objetivo_global->elements_count; i++) {
		objetivo = list_get(objetivo_global, i);
		pthread_create(&thread, NULL, enviarGetPokemon, objetivo->pokemon);
		pthread_detach(thread);
	}
}

void iniciarPlanificador(void) {
	// Listas de estados
	entrenadores_new = list_create();
	entrenadores_ready = list_create();
	entrenadores_blocked_idle = list_create();
	entrenadores_blocked_waiting_caught = list_create();
	entrenadores_blocked_full = list_create();
	entrenadores_exit = list_create();

	pokemones_en_el_mapa = queue_create();
	retardo_ciclo_cpu = config_get_int_value(config, "RETARDO_CICLO_CPU");



	sem_init(&counter_pokemones_libres, 0, 0);	// inicio el semaforo en 0 ya que todavia no tengo pokemones.

	pthread_t thread;

	pthread_create(&thread, NULL, mandarABuscarPokemones, NULL);
}


//////////////////////////////////////
//				EJECUCION			//
//////////////////////////////////////

	//////////////////////////////////////
	//			PLANI LARGO PLAZO		//
	//////////////////////////////////////

t_tcb* entrenadorMasCercanoA(t_pokemon_en_mapa* pokemon, t_list** lista) {

	t_tcb* entrenador_temp = NULL;
	int distancia_temp;
	int menor_distancia;
	t_tcb* entrenador_mas_cercano;
	int index;

	log_debug(logger, "Tamaño de entrenadores_blocked_idle: %d", entrenadores_blocked_idle->elements_count);

	// busco en blocked_idle
	if (!list_is_empty(entrenadores_blocked_idle)) {
		entrenador_mas_cercano = list_get(entrenadores_blocked_idle, 0);
		menor_distancia = distanciaA(entrenador_mas_cercano->entrenador->posicion, pokemon->posicion);
		*lista = entrenadores_blocked_idle;

		for (index = 1; index < list_size(entrenadores_blocked_idle); index++) {
			if (menor_distancia == 0) break;	// corto porque sería la distancia minima, entonces dejo de recorrer

			entrenador_temp = list_get(entrenadores_blocked_idle, index);
			distancia_temp = distanciaA(entrenador_temp->entrenador->posicion, pokemon->posicion);

			if (distancia_temp < menor_distancia) {

				entrenador_mas_cercano = entrenador_temp;
				menor_distancia = distancia_temp;
			}
		}
		// Si ya tengo al minimo, directamente salgo
		if (menor_distancia == 0) return entrenador_mas_cercano;
	}

	// Busco en new
	if (!list_is_empty(entrenadores_new)) {

		index = 0;

		if (list_is_empty(entrenadores_blocked_idle)) { // Si no se busco en los bloqueados
			entrenador_mas_cercano = list_get(entrenadores_new, index++);
			menor_distancia = distanciaA(entrenador_mas_cercano->entrenador->posicion, pokemon->posicion);
			*lista = entrenadores_new;
		}


		// Recorro en los de estado new
		for (; index < list_size(entrenadores_new); index++) {
			if (menor_distancia == 0) break;	// corto porque sería la distancia minima, entonces dejo de recorrer

			entrenador_temp = list_get(entrenadores_new, index);
			distancia_temp = distanciaA(entrenador_temp->entrenador->posicion, pokemon->posicion);

			if (distancia_temp < menor_distancia) {
				*lista = entrenadores_new;
				entrenador_mas_cercano = entrenador_temp;
				menor_distancia = distancia_temp;
			}
		}
	}

	return entrenador_mas_cercano;
}

void *mandarABuscarPokemones(void) { //Pasar de new/blocked_idle a ready (Planificador a largo plazo)
	t_tcb* tcb_entrenador; // entrenador a pasar a ready - desde new o blocked_idle
	t_pokemon_en_mapa* pokemon;
	t_list* lista_actual = NULL; // Lista en la que se encuentra el entrenador mas cercano

	while(1)  { // TODO no esté en exit el team
		log_debug(logger, "Voy a esperar a que haya pokemones libres");
		sem_wait(&counter_pokemones_libres);
		sem_wait(&counter_entrenadores_disponibles);
		log_debug(logger, "Hay un pokemon disponible para buscarlo");
		pokemon = queue_pop(pokemones_en_el_mapa);
		tcb_entrenador = entrenadorMasCercanoA(pokemon, &lista_actual);

		log_debug(logger, "El entrenador %d va a ir a buscarlo", tcb_entrenador->entrenador->id_entrenador);

		tcb_entrenador->entrenador->destino = pokemon->posicion;

		log_debug(logger, "cantidad de lista actual = %d", lista_actual->elements_count);
		cambiarDeLista(tcb_entrenador, lista_actual, entrenadores_ready);
	}
}

void realizarCicloDeCPU(void) {
	realizarXCiclosDeCPU(1);
}

void realizarXCiclosDeCPU(int cant_ciclos) {
	sleep(retardo_ciclo_cpu* cant_ciclos);
}



//////////////////////////////////////
//			OBJETIVOS + MAPA		//
//////////////////////////////////////

int pokemonNecesario(t_pokemon* pokemon) {
	t_inventario* inventario = buscarInventarioPorPokemonName(objetivo_global, pokemon->name);
	// Si no lo encuentro o si no necesito mas
	return !(inventario == NULL || inventario->cantidad == 0);
	// TODO aca va el verificar si ya se recibio un mensaje de este pokemon?
}

void agregarPokemonAlMapa(t_pokemon* pokemon, t_coords* posicion) {
	t_pokemon_en_mapa* pok_mapa = malloc(sizeof(t_pokemon_en_mapa));
	pok_mapa->pokemon = pokemon;
	pok_mapa->posicion = posicion;
	pok_mapa->disponible = 1;

	queue_push(pokemones_en_el_mapa, pok_mapa);
	sem_post(&counter_pokemones_libres);
}
