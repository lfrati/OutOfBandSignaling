#!/bin/bash

trap '' INT # per ora non voglio essere interrotto
set -e # in caso di errore termino lo script

# attenzione: i client vengono lanciati a coppie quindi il primo argomento
#deve essere la metà del numero di client che vogliamo lanciare
# $1: (numero di client)/2 
# $2: p
# $3: k
# $4: w

#poichè il massimo secret è 3 secondi ed ogni sleep nel ciclo dura 10, devo fare almeno (num_invii*3)/10 cicli
attesa_necessaria=$(($4*3/10)) # per il test con parametri 5 8 20 vale 6 e quindi attenderò 60 secondi

#echo "Test will require $(($1+$attesa_necessaria*10)) seconds"

# lancio il supervisor con k server
./my_supervisor $3 1>>supervisor_log &

#echo "Launched Supervisor"

superv=$(pidof my_supervisor)

trap 'kill -INT $superv; kill -INT $superv; exit' INT # in caso di ctr-c termino l'esecuzione ed esco

sleep 2 # attendo due secondi e comincio a lanciare i client

#echo "Launching clients"

# lancio due client ogni secondo
for ((i=0; i < $1; i++)); do
	./my_client $2 $3 $4 1>>client_log &
	./my_client $2 $3 $4 1>>client_log &
	sleep 1
done

#echo "Clients launched: $(($i*2))" 

# ogni dieci secondi invio un SIGINT al supervisor
for (( i = 0; i < $attesa_necessaria; i++ )); do
	kill -INT $superv
	sleep 10
done

# doppio SIGINT per terminare
kill -INT $superv
sleep 0.2
kill -INT $superv

./misura.sh supervisor_log client_log
