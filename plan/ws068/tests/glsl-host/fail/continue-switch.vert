#version 130
// expect: 0:9: error: continue inside a switch is not supported
void main()
{
	int i;
	for (i = 0; i < 4; i++) {
		switch (i) {
		case 1:
			continue;
		default:
			break;
		}
	}
	gl_Position = vec4(float(i));
}
