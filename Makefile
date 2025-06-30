SRC = src
LIB = lib

build b:
	$(MAKE) build --directory=$(SRC) -s

dbg d:
	$(MAKE) dbg --directory=$(SRC) -s

clean c:
	$(MAKE) clean --directory=$(SRC) -s

test t:
	$(MAKE) test --directory=$(LIB) -s
