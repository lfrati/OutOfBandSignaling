#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>

/*Da: Advanced Unix Programming 2nd Edition pag. 97*/
ssize_t readall(int fd, void *buf, size_t nbyte){
	ssize_t nread = 0, n;
	do{
		if((n = read(fd, &((char *)buf)[nread], nbyte - nread)) == -1 ){
			if( errno == EINTR)
				continue;
			else
				return -1;
		}
		if(n == 0)
			return nread;

		nread += n;
	} while(nread < nbyte);

	return nread;
}


/*Da: Advanced Unix Programming 2nd Edition pag. 95*/
ssize_t writeall(int fd, const void *buf, size_t nbyte){
	ssize_t nwritten = 0, n;
	do{
		if((n = write(fd, &((const char *)buf)[nwritten], nbyte - nwritten)) == -1 ){
			if( errno == EINTR)
				continue;
			else
				return -1;
		}
		nwritten += n;
	} while(nwritten < nbyte);

	return nwritten;
}

int is_littleendian(){
	uint32_t v = 0x01234567;
	/* con il puntatore castato ad uint8_t accedo al bit più significativo, se contiene 67 allora la macchina è little-endian (vedere relazione per spiegazione) */
	return ((*((uint8_t*)(&v))) == 0x67);
}