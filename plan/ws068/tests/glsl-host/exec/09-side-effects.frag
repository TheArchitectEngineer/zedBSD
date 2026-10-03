// expect: 0 255 0 255
// Short-circuit && and ||, ?: with side effects, the comma operator, assignments as values.
precision highp float;

int calls;

bool touch(bool value)
{
	calls++;
	return value;
}

void main()
{
	int failed = 0;
	int i = 0;
	float x;
	float y;

	calls = 0;
	if (false && touch(true)) failed = 1;
	if (calls != 0) failed = 2;
	if (!(true || touch(false))) failed = 3;
	if (calls != 0) failed = 4;
	if (!(true && touch(true))) failed = 5;
	if (calls != 1) failed = 6;
	x = (i > 0) ? float(i++) : float(i--);
	if (x != 0.0 || i != -1) failed = 7;
	x = (i++, i++, float(i));
	if (x != 1.0) failed = 8;
	x = y = 3.0;
	if (x != 3.0 || y != 3.0) failed = 9;
	y = (x += 1.0) * 2.0;
	if (y != 8.0) failed = 10;
	x = (calls > 0) ? 5.0 : 6.0;
	if (x != 5.0) failed = 11;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
