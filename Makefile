build:
	@gcc fuzzy.c term_escapes.c term_mode.c main.c -o fz

leak_check:
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./fz
	@printf "some\nthing\nanother\n" | valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./fz
	@cd empty_dir && valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ../fz

install: build
	@cp fz /usr/local/bin/fz

uninstall:
	@rm /usr/local/bin/fz
