#!/bin/bash

grep "BASED" $1 > supervisor_tmp 	# filtro soltanto le linee che mi interessano, quelle con le stime
grep "SECRET" $2 > client_tmp		# e quelle con i secret

declare -A superv_secr #superv_secr avrà in posizione ID il secret stimato per il client con quell'ID 

sbagliate=0 # numero di stime sbagliate
corrette=0	# chissà questa invece cosa conta
errori=0	# accumulatore degli errori di stima commessi

# le righe di supervisor_tmp sono della forma "SUPERVISOR ESTIMATE 123 FOR 456 BASED ON 789"
while read line
do
	#in riferimento alla riga di esempio
	line=${line%% BASED*} # "SUPERVISOR ESTIMATE 123 FOR 456"
	ID=${line##*FOR } # "456"
	line=${line%% FOR*} # "SUPERVISOR ESTIMATE 123"
	SECRET=${line##*ESTIMATE } # "123"

    superv_secr[$ID]=$SECRET # salvo la stima per il secret del client con questo ID
    
done < supervisor_tmp

#le righe di client_tmp sono della forma "CLIENT 123 SECRET 456"
while read line
do
	#in riferimento alla riga di esempio
	SECRET=${line##*SECRET } # "456"
	line=${line%% SECRET*} # "CLIENT 123"
	ID=${line##*CLIENT } # "123"
	
	if [ -z "${superv_secr[$ID]}" ]; then 
            sbagliate=$(($sbagliate+1)) # il supervisor per qualche motivo non ha ricevuto stime per ID 
       	elif [ $((${superv_secr[$ID]}-$SECRET)) -ge 0 ] && [ $((${superv_secr[$ID]}-$SECRET)) -le 25 ]; then
            corrette=$(($corrette+1)) # la differenza tra stima e secret è minore di 25
            errori=$(($errori+$((${superv_secr[$ID]}-$SECRET))))
       	else 
       		sbagliate=$(($sbagliate+1)) # la differenza tra stima e secret è maggiore di 25
       		errori=$(($errori+$((${superv_secr[$ID]}-$SECRET))))
	fi

done < client_tmp

totali=$(($corrette+$sbagliate)); # un'altra variable il cui scopo è un mistero

percent_succ=$(($corrette*100/$totali)); # percentuale di successo
err_medio=$(($errori/$totali)) # errore medio delle stime

echo "Successi: $corrette"
echo "Fallimenti: $sbagliate"
echo "Percentuale di successo: $percent_succ %"
echo "Errore medio di stima: $err_medio"

rm supervisor_tmp # elimino i file temporanei che ho usato
rm client_tmp