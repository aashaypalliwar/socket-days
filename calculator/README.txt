Compile:
1. gcc -o c client.c
2. gcc -o s server.c -lm -lpthread

Note:
Supported operations:
1. ADD x y => outputs x + y
2. SUB x y => outputs x - y
3. MUL x y => outputs x * y
4. DIV x y => outputs x / y
5. MOD x y => outputs x % y
6. EXP x => outputs exp(x)
7. LN x => outputs ln(x)
8. CLOSE => closes calculator

At max "maxCal" number of calculators can be operated.
We have used threads for simultaneous operation of "maxCal" number of calculators.
