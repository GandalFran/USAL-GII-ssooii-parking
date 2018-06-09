//--------------- TIPOS DE MENSAJE --------------------------------------------------------
#define TIPO_COMANDO                1
#define TIPO_REQUEST                2
#define __TIPO_ORDEN(algoritmo)     (20 + algoritmo)
#define __TIPO_RESERVAS(algoritmo)  (30 + algoritmo)    
#define __TIPO_OCUPADOS(algoritmo)  (40 + algoritmo)
//estructura tipos mensajes: (coche*100 (n cifras)) + (tipo msg-algoritmo ( 2 cifras))
#define TIPO_ORDEN(coche,algoritmo)     ((100*(coche)) + __TIPO_ORDEN(algoritmo))
#define TIPO_RESERVA(coche,algoritmo)   ((100*(coche)) + __TIPO_RESERVAS(algoritmo))
#define TIPO_OCUPACION(coche,algoritmo) ((100*(coche)) + __TIPO_OCUPADOS(algoritmo))

//--------------- SEMAFOROS --------------------------------------------------------
#define SEM_START           (PARKING_getNSemAforos())
#define SEM_WRITE(algoritmo)    (PARKING_getNSemAforos() + 1 +     (algoritmo))
#define SEM_READ(algoritmo)     (PARKING_getNSemAforos() + 1 + 4 + (algoritmo))

//--------------- SECCIONES CRITICAS --------------------------------------------------
void entrarEscrituraSC(){ 
    /*Inicio atomicas*/
    wait0(semWrite);		
    signal(semWrite);		
    /*Fin atomicas*/		
    wait0(semRead); 
}
void salirEscrituraSC(){     
	wait(semWrite);
}
void entrarLecturaSC(){
    /*Inicio atomicas*/
    wait0(semWrite);
    signal(semRead);
  	/*Fin atomicas*/
}
void salirEscrituraSC(){
	wait(semRead);                   
}

//--------------- FUNCIONES PROGRAMA --------------------------------------------------

int main(...){
	...
	wait(semStart);
	wait0(semStart);
	...
}

void mailManagerFIFO(){
	wait(semStart);
	wait0(semStart);

    while(TRUE){
        reciveB(msg1 de tipo biblioteca);
        cambiar tipo msg1 a TIPO_COMANDO

        reciveB(msg2 de TIPO_PETICION);
        send(msg1 a solicitador);
    }
}

void mailManagerPA(){
	wait(semStart);
	wait0(semStart);

	//Orden de llegada de los mensajes
    orden = 1;

    while(TRUE){
        while(BH no lleno){
            reciveNB(msg tipo PARKING_MSG);
            if(no se recibio ningun mensaje) break;

            cambiar tipo de msg a TIPO_COMANDO
            /*Al sumar orden a la clave hacemos que los mensajes dentro de cada 
            categoria salgan en el orden que fueron recogidos del buzon*/
            if(msg.subtipo == APARCAR){
            	elementoBH.clave = PRIORITARIO + orden;
            }else{ 
            	elementoBH.clave = NO_PRIORITARIO + orden;
            }
            elementoBH.info = msg;
            insertarBH(msg);
            orden++;
        }

        if(not vacio BH){
            msg = eliminarMinimoBH().info;
        }
        else{
            reciveB(msg tipo PARKING_MSG);
            cambiar tipo de msg a TIPO_COMANDO
            orden++;
        }

        //Espera por la peticion de mensaje de un chofer para enviar el mensaje recogido
        reciveB(msg2 de TIPO_PETICION);
        send(msg a solicitador);
    }
}

void mailManagerPD(){
  	/*Igual que mailManagerPA pero cambia el if-else dentro del while mas interno*/
	if(msg.subtipo == DESAPARCAR){
        elementoBH.clave = PRIORITARIO + orden;
    }else{ 
        elementoBH.clave = NO_PRIORITARIO + orden;
    }
}

//----------- FUNCION DE LOS PROCESOS CHOFERES ----------------------------------------
void choferFunction(){

	wait(semStart);
	wait0(semStart);

    while(TRUE){
        send(msg tipo TIPO_REQUEST)
        reciveB(msg tipo TIPO_COMANDO);
        
        if(msg.subtipo == APARCAR){
        	reciveB( msg TIPO_ORDEN(msg.coche.getNumero(),msg.coche.getAlgoritmo()) );
            PARKING_aparcar(...);    
        }else{
            PARKING_desaparcar(...);
        }
    }
}

//----------- FUNCIONES DE CALLBACK DE INTERFAZ CON LA BIBLIOTECA ---------------------
void commit(coche){
    send( msg TIPO_ORDEN(coche.getNumero()+1,coche.getAlgoritmo()) );
}

void permisoAvance(coche){
    if( esta en la carretera y no no se esta saliendo de la carretera){
        posReserva = coche.getX2();
        posOcupacionInicio = posOcupacionFin = coche.getX2();
    }else if( esta desaparcando ){
        posReserva = coche.getX2() + coche.getLongitud() - 1; 
        posOcupacionInicio = coche.getX2();                     
        posOcupacionFin = posReserva;                               
    }

    reservarCarretera(...);
    pedirPermisoOcupacion(...);
}

void permisoAvanceCommit(coche c){

    if( esta desaparcando ){
        liberarAcera(...);
    }

    actualizarCarretera(...);
    recepcionPeticionReserva(...);
    recepcionPeticionOcupacion(...);
}

//----------- FUNCIONES DE ESCRITURA EN MEMORIA COMPARTIDA ----------------------------
void actualizarCarretera(coche, carretera){
    if( esta desaparcando ){
        entrarEscrituraSC(...);
        ocuparCarretera(desde coche.getX() ... hasta coche.getLongitud()-1);
        salirEscrituraSC(...);
    }
    else if( esta aparcando ){
        entrarEscrituraSC(...);
        liberarCarretera(desde coche.getX() ... hasta coche.getLongitud()-1);
        salirEscrituraSC(...);
    }
    else if( esta en la carretera){
        entrarEscrituraSC(...);
        if( si no estamos empezando a salirnos de la carretera ){
            ocuparYDesreservar(coche.getX());
        }
        if( si ya hemos entrado en la carretera ){
            liberar(coche.getX()+coche.getLongitud());        
        }
        salirEscrituraSC(...);
    }
}

void reservarCarretera(coche, carretera, posicion){
    entrarEscrituraSC(...);

    while(posicion esta reservada/reservada+ocupada y no la estamos reservando nosotros){
        send(msg con TIPO_RESERVA(idReservante, coche.getAlgoritmo()) );
        salirEscrituraSC(...);
        reciveB(msg con TIPO_RESERVA(coche.getNUmero(),coche.getAlgoritmo()) );
        entrarEscrituraSC(...);
    }
        
    reservarPosicion(...);
    salirEscrituraSC(...);  
}

//----------- FUNCIONES DE LECTURA EN MEMORIA COMPARTIDA ------------------------------
void pedirPermisoOcupacion(coche, carretera, posInicial, posFinal){

    entrarLecturaSC(c);

    for(i = posFinal; i >= posInicial; i--){
        if(si carretera[i] esta reservada u ocupada)
            break;
    }

    if(si hay alguna posicion ocupada){
        send(msg con TIPO_OCUPACION(idOcupante,coche.getAlgoritmo()) y posX = posInicial);
        salirLecturaSC(c);

        while(TRUE){
            reciveB( msg con TIPO_OCUPACION(coche.getNUmero(),coche.getAlgoritmo()) );
            if(si nos lo envio el coche que estaba ocupando la posicion i )
                break;
            else
                guardarPeticionOcupacion(¡);
        }
    }
    else{
        salirLecturaSC(c);
    }   
}

//----------- FUNCIONES DE GESTION DE PETICIONES AJENAS -------------------------------
void recepcionPeticionReserva(coche){
    if( esta aparcando )
        return;

    reciveNB(msg con TIPO_RESERVA(coche.getNUmero(),coche.getAlgoritmo()) );
    if( hemos recibido algun mensaje ){
        send(msg con tipo TIPO_RESERVA(msg.idRemitente, coche.getAlgoritmo(c)) );
    }
}

void recepcionPeticionOcupacion(coche){

    if( esta desaparcando )
        return;

    for(i = 0; i < MAX_NUMERO_PETICIONES_PENDIENTES; i++){
        if(hay una peticion en esta posicion Y (esta aparcando O la peticion viene para la posicion que he desocupado)){
            send( msg con TIPO_OCUPACION(idRemitente, coche.getAlgoritmo(c)) );
        }
    }

    reciveNB(msg con TIPO_OCUPACION(coche.getNUmero(),coche.getAlgoritmo()) );
    while( siga habiendo peticiones ){
    	if(esta aparcando O la peticion viene para la posicion que he desocupado)
            send( msg con TIPO_OCUPACION(idRemitente, coche.getAlgoritmo(c)) );
        else
            guardarPeticionOcupacion(c, msg);

       	reciveNB(msg con TIPO_OCUPACION(coche.getNUmero(),coche.getAlgoritmo()) );
    }
}