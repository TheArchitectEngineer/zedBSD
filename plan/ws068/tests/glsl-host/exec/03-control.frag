// expect: 0 255 0 255
// Control flow: if and else, for with break and continue, while, do-while, nested loops.
// uniform: u_limit 10
precision highp float;
uniform float u_limit;

void main()
{
	int failed = 0;
	float sum = 0.0;
	int count = 0;
	int i;
	int j;
	float x;

	for (i = 0; i < 10; i++) {
		if (i == 3)
			continue;
		if (i == 8)
			break;
		sum += float(i);
	}
	if (sum != 25.0) failed = 1;

	x = 0.0;
	while (x < u_limit)
		x += 3.0;
	if (x != 12.0) failed = 2;

	i = 0;
	do {
		i += 2;
	} while (i < 7);
	if (i != 8) failed = 3;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			if (j > i)
				break;
			count++;
		}
	}
	if (count != 10) failed = 4;

	if (u_limit > 5.0) {
		x = 1.0;
	} else if (u_limit > 1.0) {
		x = 2.0;
	} else {
		x = 3.0;
	}
	if (x != 1.0) failed = 5;

	count = 0;
	for (int k = 0; k < 3; k++)
		for (int m = 0; m < 3; m++)
			count += k * m;
	if (count != 9) failed = 6;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
