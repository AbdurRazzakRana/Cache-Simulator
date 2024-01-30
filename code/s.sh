rm -f sim main.o cache.o
make
./sim -bs 4 -a 1 -wb -wa ../traces/spice10.trace
