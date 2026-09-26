// expect: 0 255 0 255
// Local structs and arrays: construction, members, dynamic indices, copies, comparison.
// uniform: u_index 2
precision highp float;
uniform int u_index;

struct Pair {
	float a;
	vec2 b;
};

struct Nested {
	Pair pair;
	int count;
};

void main()
{
	int failed = 0;
	Pair p = Pair(1.0, vec2(2.0, 3.0));
	Pair q;
	Nested n = Nested(Pair(4.0, vec2(5.0)), 6);
	float values[4];
	vec2 points[3];
	int i;

	q = p;
	q.b.y = 7.0;
	if (p.b.y != 3.0 || q.b.y != 7.0) failed = 1;
	if (n.pair.b.x != 5.0 || n.count != 6) failed = 2;
	for (i = 0; i < 4; i++)
		values[i] = float(i * i);
	if (values[u_index] != 4.0) failed = 3;
	values[u_index + 1] = 20.0;
	if (values[3] != 20.0) failed = 4;
	points[0] = vec2(1.0);
	points[1] = vec2(2.0);
	points[2] = vec2(3.0);
	points[u_index].y = 9.0;
	if (points[2] != vec2(3.0, 9.0)) failed = 5;
	if (p == q) failed = 6;
	q.b.y = 3.0;
	if (p != q) failed = 7;
	n.pair = p;
	if (n.pair.a != 1.0) failed = 8;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
