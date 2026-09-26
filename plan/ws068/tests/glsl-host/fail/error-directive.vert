// expect: 0:4: error: #error stop here
#define STOP 1
#if STOP
#error stop here
#endif
void main()
{
}
