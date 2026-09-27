/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The functions of the mathematical library not yet rewritten (WS076).
 *
 * This is the remainder of the former src/libc/math.c.  Each phase of
 * WS076 moves functions out of it into their own files, and the file goes
 * away when the last of them has moved.
 */

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#include "src/libc/math/math-internal.h"

#define ZM_PI 3.14159265358979323846264338327950288
#define ZM_PI_2 1.57079632679489661923132169163975144
#define ZM_PI_4 0.78539816339744830961566084581987572
#define ZM_LN2 0.69314718055994530941723212145817657

int signgam;
double cbrt(double x){double a=fabs(x),g;int i;if(a==0||isinf(a)||isnan(a))return x;g=exp(log(a)/3);for(i=0;i<4;i++)g=(2*g+a/(g*g))/3;return x<0?-g:g;}
float cbrtf(float x){return (float)cbrt(x);} long double cbrtl(long double x){return (long double)cbrt((double)x);}
double hypot(double x,double y){x=fabs(x);y=fabs(y);if(x<y){double t=x;x=y;y=t;}if(isinf(x))return x;if(x==0)return 0;y/=x;return x*sqrt(1+y*y);}
float hypotf(float x,float y){return (float)hypot(x,y);} long double hypotl(long double x,long double y){return (long double)hypot((double)x,(double)y);}


/* Abramowitz-Stegun 7.1.26, maximum error about 1.5e-7. */
double erf(double x){double s=x<0?-1:1,t,a;x=fabs(x);t=1/(1+0.3275911*x);a=1-(((((1.061405429*t-1.453152027)*t+1.421413741)*t-0.284496736)*t+0.254829592)*t)*exp(-x*x);return s*a;}
float erff(float x){return (float)erf(x);} long double erfl(long double x){return (long double)erf((double)x);}
double erfc(double x){return 1-erf(x);} float erfcf(float x){return (float)erfc(x);} long double erfcl(long double x){return (long double)erfc((double)x);}

/* Lanczos approximation with g=7 and nine coefficients. */
static double
gamma_lanczos(double z)
{
	static const double c[] = {0.99999999999980993,676.5203681218851,
	    -1259.1392167224028,771.32342877765313,-176.61502916214059,
	    12.507343278686905,-0.13857109526572012,9.9843695780195716e-6,
	    1.5056327351493116e-7};
	double a,t;int i;
	if(z<0.5)return ZM_PI/(sin(ZM_PI*z)*gamma_lanczos(1-z));
	z-=1;a=c[0];for(i=1;i<9;i++)a+=c[i]/(z+i);t=z+7.5;
	return 2.5066282746310005024*sqrt(t)*pow(t,z)*exp(-t)*a;
}
double tgamma(double x){if(x<=0&&x==trunc(x)){errno=EDOM;return NAN;}return gamma_lanczos(x);} float tgammaf(float x){return (float)tgamma(x);} long double tgammal(long double x){return (long double)tgamma((double)x);}
double lgamma(double x){double g=tgamma(x);signgam=g<0?-1:1;return log(fabs(g));} float lgammaf(float x){return (float)lgamma(x);} long double lgammal(long double x){return (long double)lgamma((double)x);}

/* Bessel functions use their defining power series for moderate arguments;
 * recurrence supplies integral orders. */
double j0(double x){double term=1,sum=1,q=x*x/4;int k;for(k=1;k<30;k++){term*=-q/(k*k);sum+=term;}return sum;}
double j1(double x){double term=x/2,sum=term,q=x*x/4;int k;for(k=1;k<30;k++){term*=-q/(k*(k+1.0));sum+=term;}return sum;}
double jn(int n,double x){int k;double a,b,c;if(n<0)return(n&1)?-jn(-n,x):jn(-n,x);if(n==0)return j0(x);if(n==1)return j1(x);if(x==0)return 0;a=j0(x);b=j1(x);for(k=1;k<n;k++){c=2.0*k*b/x-a;a=b;b=c;}return b;}
float j0f(float x){return (float)j0(x);} float j1f(float x){return (float)j1(x);} float jnf(int n,float x){return (float)jn(n,x);}
double y0(double x){if(x<=0){errno=EDOM;return -INFINITY;}return sqrt(2/(ZM_PI*x))*sin(x-ZM_PI_4);}
double y1(double x){if(x<=0){errno=EDOM;return -INFINITY;}return sqrt(2/(ZM_PI*x))*sin(x-3*ZM_PI_4);}
double yn(int n,double x){int k;double a,b,c;if(n==0)return y0(x);if(n==1)return y1(x);a=y0(x);b=y1(x);for(k=1;k<n;k++){c=2.0*k*b/x-a;a=b;b=c;}return b;}
float y0f(float x){return (float)y0(x);} float y1f(float x){return (float)y1(x);} float ynf(int n,float x){return (float)yn(n,x);}
