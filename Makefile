IDIR =./deps/include
CC=emcc

ODIR=build/obj
LDIR=./deps/lib

CFLAGS=-I$(IDIR) -L$(LDIR) -O3

LIBS=deps/lib/libavformat.a deps/lib/libavcodec.a deps/lib/libavresample.a deps/lib/libswscale.a deps/lib/libavutil.a deps/lib/libx264.so deps/lib/libz.a

DEPS = 

_OBJ = main.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

EM_SETTINGS=-s WARN_ON_UNDEFINED_SYMBOLS=1 -s EXPORTED_FUNCTIONS="['_main', '_start_recording', '_add_video_frame', '_end_recording']" -s ALLOW_MEMORY_GROWTH=1 --pre-js pre.js

$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

recorder.js: $(OBJ) pre.js worker-pre.js worker-post.js
	$(CC) -o $@ $(OBJ) $(CFLAGS) $(LIBS) --pre-js worker-pre.js $(EM_SETTINGS) --post-js worker-post.js

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~ recorder.js recorder.js.mem