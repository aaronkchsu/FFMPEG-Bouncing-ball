all: bounce

bounce: bouncer.c
	gcc -o bouncer bouncer.c `pkg-config --cflags --libs libavutil libavformat libavcodec libswscale` 

.PHONY: clean

clean:
	rm -f bouncer *.mpff movie.mp4 *.o

movie:
	ffmpeg -framerate 30 -start_number 000 -i frame%03d.mpff movie.mp4
