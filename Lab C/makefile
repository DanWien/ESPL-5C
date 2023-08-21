all: clean myshell mypipeline

myshell:
	gcc -m32 -g -Wall -o myshell myshell.c

mypipeline:
	gcc -m32 -g -Wall -o mypipeline mypipeline.c

.PHONY: clean
clean:
	rm -f myshell.o myshell mypipeline