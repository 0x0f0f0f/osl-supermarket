CC = clang
CFLAGS = -Wall -g -std=gnu99 -D_POSIX_C_SOURCE=200809L
LIBS = -lpthread -lm
OPTFLAGS = -O3
LDFLAGS = -L.
INCLUDES = -I.
TARGETS = manager supermarket
OBJECTS = lqueue.o conc_lqueue.o linked_list.o

.PHONY: all bin clean sanitize prod debug
.SUFFIXES: .c .h

# Default to optimized production target.
default: prod

prod: CFLAGS+=$(OPTFLAGS)
prod: all

# Use clang thread sanitizer.
sanitize: CFLAGS+=-fno-omit-frame-pointer -fsanitize=thread
sanitize: debug

# Add debug flags.
debug: CFLAGS+=-g
debug: LOGLEVEL+=-DLOG_LVL=LOG_LVL_NEVER
debug: LOGLEVEL+=-DLOG_SYSCALL
debug: clean all

all: $(OBJECTS) $(TARGETS)

manager: $(OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -o $@ manager.c $(OBJECTS)

supermarket: $(OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -o $@ supermarket.c $(OBJECTS)
	
%.o: %.c %.h
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -c -o $@ $<

clean:
	$(RM) -f $(TARGETS) *.o

test1:
	echo hello

test2:
	echo hello
	
