gcc TX/write_noncanonical.c src/linklayer.c -o TX/write
gcc RX/read_noncanonical.c src/linklayer.c -o RX/read
gcc test/main.c src/linklayer.c -o test/test