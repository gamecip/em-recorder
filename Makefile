IDIR =./deps/include
CC=emcc

ODIR=build/obj
LDIR=./deps/lib

CFLAGS=-I$(IDIR) -L$(LDIR)

LIBS=deps/lib/libavformat.a deps/lib/libavcodec.a deps/lib/libavresample.a deps/lib/libswscale.a deps/lib/libavutil.a deps/lib/libx264.so deps/lib/libz.a

DEPS = 

_OBJ = main.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

EM_SETTINGS=-s EXPORTED_FUNCTIONS="['_main', '_add_video_frame', '_end_recording']" -s TOTAL_MEMORY=33554432 -s ALLOW_MEMORY_GROWTH=1 --pre-js deps/base64-js/base64js.min.js -s MODULARIZE=1 -s EXPORT_NAME=Recorder

$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

emr.js: $(OBJ) pre.js
	$(CC) -o $@ $(OBJ) $(CFLAGS) $(LIBS) $(EM_SETTINGS)

emr.html: $(OBJ) pre.js
	$(CC) -o $@ $(OBJ) $(CFLAGS) $(LIBS) $(EM_SETTINGS) --pre-js pre.js

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~ emr.js emr.html
