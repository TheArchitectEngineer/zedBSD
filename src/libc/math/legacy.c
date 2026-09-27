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
static double
reduce_angle(double x, int *quadrant)
{
	double q = rint(x / ZM_PI_2);
	*quadrant = (int)q & 3;
	return x - q * ZM_PI_2;
}

static double
sin_kernel(double x)
{
	double x2=x*x;
	return x*(1.0+x2*(-1.0/6.0+x2*(1.0/120.0+x2*(-1.0/5040.0+
	    x2*(1.0/362880.0+x2*(-1.0/39916800.0+x2/6227020800.0))))));
}
static double
cos_kernel(double x)
{
	double x2=x*x;
	return 1.0+x2*(-1.0/2.0+x2*(1.0/24.0+x2*(-1.0/720.0+
	    x2*(1.0/40320.0+x2*(-1.0/3628800.0+x2/479001600.0)))));
}
double sin(double x){int q;double r;if(isnan(x))return x;if(isinf(x)){errno=EDOM;return NAN;}r=reduce_angle(x,&q);return q==0?sin_kernel(r):q==1?cos_kernel(r):q==2?-sin_kernel(r):-cos_kernel(r);}
double cos(double x){int q;double r;if(isnan(x))return x;if(isinf(x)){errno=EDOM;return NAN;}r=reduce_angle(x,&q);return q==0?cos_kernel(r):q==1?-sin_kernel(r):q==2?-cos_kernel(r):sin_kernel(r);}
double tan(double x){return sin(x)/cos(x);} float sinf(float x){return (float)sin(x);} float cosf(float x){return (float)cos(x);} float tanf(float x){return (float)tan(x);} long double sinl(long double x){return (long double)sin((double)x);} long double cosl(long double x){return (long double)cos((double)x);} long double tanl(long double x){return (long double)tan((double)x);}

double
atan(double x)
{
	double sign=1.0, result, term, x2;
	int i;
	if (isnan(x))
		return x;
	if (x < 0) {
		sign = -1;
		x = -x;
	}
	if(x>1.0)return sign*(ZM_PI_2-atan(1.0/x));
	if(x>0.4142135623730950)return sign*(ZM_PI_4+atan((x-1.0)/(x+1.0)));
	x2=x*x;term=x;result=x;for(i=3;i<=39;i+=2){term*=-x2;result+=term/i;}return sign*result;
}
float atanf(float x){return (float)atan(x);} long double atanl(long double x){return (long double)atan((double)x);}
double atan2(double y,double x){if(isnan(x)||isnan(y))return NAN;if(x>0)return atan(y/x);if(x<0)return y>=0?atan(y/x)+ZM_PI:atan(y/x)-ZM_PI;if(y>0)return ZM_PI_2;if(y<0)return -ZM_PI_2;return y;}
float atan2f(float y,float x){return (float)atan2(y,x);} long double atan2l(long double y,long double x){return (long double)atan2((double)y,(double)x);}
double asin(double x){if(fabs(x)>1){errno=EDOM;return NAN;}return atan2(x,sqrt((1.0-x)*(1.0+x)));}
float asinf(float x){return (float)asin(x);} long double asinl(long double x){return (long double)asin((double)x);}
double acos(double x){return ZM_PI_2-asin(x);} float acosf(float x){return (float)acos(x);} long double acosl(long double x){return (long double)acos((double)x);}

double sinh(double x){double e=exp(x),i=1.0/e;return 0.5*(e-i);} float sinhf(float x){return (float)sinh(x);} long double sinhl(long double x){return (long double)sinh((double)x);}
double cosh(double x){double e=exp(fabs(x));return 0.5*(e+1.0/e);} float coshf(float x){return (float)cosh(x);} long double coshl(long double x){return (long double)cosh((double)x);}
double tanh(double x){if(x>20)return 1;if(x<-20)return -1;{double e=exp(2*x);return(e-1)/(e+1);}} float tanhf(float x){return (float)tanh(x);} long double tanhl(long double x){return (long double)tanh((double)x);}
double asinh(double x){return log(x+sqrt(x*x+1));} float asinhf(float x){return (float)asinh(x);} long double asinhl(long double x){return (long double)asinh((double)x);}
double acosh(double x){if(x<1){errno=EDOM;return NAN;}return log(x+sqrt((x-1)*(x+1)));} float acoshf(float x){return (float)acosh(x);} long double acoshl(long double x){return (long double)acosh((double)x);}
double atanh(double x){if(fabs(x)>=1){errno=EDOM;return x<0?-INFINITY:INFINITY;}return 0.5*log((1+x)/(1-x));} float atanhf(float x){return (float)atanh(x);} long double atanhl(long double x){return (long double)atanh((double)x);}

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
