IDIR =./deps/include
CC=emcc

ODIR=build/obj
LDIR=./deps/lib

CFLAGS=-I$(IDIR) -L$(LDIR)

LIBS=deps/lib/libavformat.a deps/lib/libavcodec.a deps/lib/libavresample.a deps/lib/libswscale.a deps/lib/libavutil.a deps/lib/libx264.so deps/lib/libz.a

DEPS = 

_OBJ = main.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

EM_SETTINGS=-s EXPORTED_FUNCTIONS="['_main', '_add_video_frame', '_end_recording']" -s ALLOW_MEMORY_GROWTH=1

$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

emr.js: $(OBJ) pre.js
	$(CC) -o $@ $(OBJ) $(CFLAGS) $(LIBS) $(EM_SETTINGS)

emr.html: $(OBJ) pre.js
	$(CC) -o $@ $(OBJ) $(CFLAGS) $(LIBS) --pre-js pre.js $(EM_SETTINGS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~ emr.js emr.html
