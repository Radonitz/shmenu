all:
	gcc main.c -o my_menu -lX11
clean:
	rm -f my_menu
