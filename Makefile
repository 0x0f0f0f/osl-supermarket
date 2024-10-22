CC = gcc
CFLAGS = -Wall -std=gnu99 -pthread -D_POSIX_C_SOURCE=2001012L
LIBS = 
OPTFLAGS = -O3
LDFLAGS = 
INCLUDES = -I.
TARGETS = manager supermarket
OBJECTS = lqueue.o conc_lqueue.o linked_list.o util.o cashcust.o ini.o
TEXCC = tectonic

.PHONY: all report test1 test2 clean tsan msan asan never prod debug
.SUFFIXES: .c .h

# Default to optimized production target.
default: prod

prod: CFLAGS+=$(OPTFLAGS)
prod: all

# Use clang thread sanitizer.
tsan: CFLAGS+=-fno-omit-frame-pointer -fsanitize=thread
tsan: debug
# Use clang address sanitizer.
asan: CFLAGS+=-fno-omit-frame-pointer -fsanitize=address
asan: debug
# Use clang memory sanitizer.
msan: CFLAGS+=-fno-omit-frame-pointer -fsanitize=memory
msan: debug


never: CFLAGS+=-g
never: LOGLEVEL+=-DLOG_SYSCALL
never: LOGLEVEL=-DLOG_LVL=LOG_LVL_NEVER
never: clean all

# Add debug flags.
debug: CFLAGS+=-g
debug: LOGLEVEL+=-DLOG_LVL=LOG_LVL_DEBUG
debug: LOGLEVEL+=-DLOG_SYSCALL
debug: all

all: $(OBJECTS) $(TARGETS)

manager: $(OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -o $@ manager.c $(OBJECTS)

supermarket: $(OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -o $@ supermarket.c $(OBJECTS)
	
%.o: %.c %.h
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LOGLEVEL) $(LIBS) -c -o $@ $<

clean:
	$(RM) -f $(TARGETS) *.o *.log

test1: debug
	./test.sh examples/test1.ini 15 SIGQUIT
test2: debug
	./manager -c examples/test2.ini &\
	./supermarket -c examples/test2.ini &
	sleep 25 && pkill -SIGHUP manager
	./analisi.sh supermarket.log

report:
	$(TEXCC) report.tex
	
