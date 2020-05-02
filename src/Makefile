CC = clang
CFLAGS = -Wall -g -std=gnu99
LIBS = -lpthread -lm
OPTFLAGS = -O3
LDFLAGS = -L.
INCLUDES = -I.
TARGETS = manager supermarket
OBJECTS =  

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
debug: LOGLEVEL+=-DLOG_LVL=LOG_LVL_DEBUG
debug: LOGLEVEL+=-DLOG_SYSCALL
debug: clean all

all: $(OBJECTS) $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -o $@ $<
	
%.o: %.c %.h
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -c -o $@ $<

clean:
	$(RM) -f $(TARGETS) *.o

test1:
	echo hello

test2:
	echo hello
	
