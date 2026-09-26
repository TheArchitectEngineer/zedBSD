// expect: 0:3: error: macro 'A' redefined differently
#define A 1
#define A 2
void main()
{
}
