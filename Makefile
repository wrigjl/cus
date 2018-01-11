
cus: cus.c
	$(CC) -Wall -Werror -O2 -o $@ $<

clean:
	rm -f cus
