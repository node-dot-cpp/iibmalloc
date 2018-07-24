
#include "test_with_lambdas.h"
#include "../../iibmalloc/iibmalloc.h"
#include <vector>
#include <functional>


class AllocInitiator
{
public:
	AllocInitiator()
	{
		g_AllocManager.initialize();
		g_AllocManager.enable();
	}
	~AllocInitiator()
	{
		g_AllocManager.disable();
		g_AllocManager.deinitialize();
	}
};

class Collable
{
public:
using callback1 = std::function<void(bool)>;
std::vector<std::pair<bool, typename callback1>> callbacks1;

using callback2 = std::function<void()>;
std::vector<std::pair<bool, typename callback2>> callbacks2;

void add1(typename callback1 cb)
{
	callbacks1.emplace_back(true, cb);
}

template<class... ARGS>
void envoke1(ARGS... args)
{
	for (auto& current : callbacks1)
		current.second(args...);
}

void clear1()
{
	callbacks1.clear();
}

void add2(typename callback2 cb)
{
	callbacks2.emplace_back(true, cb);
}

template<class... ARGS>
void envoke2(ARGS... args)
{
	for (auto& current : callbacks2)
		current.second(args...);
}

void clear2()
{
//	callbacks2.clear();
	callbacks2.erase( callbacks2.begin(), callbacks2.end() );
}
}; // Collable

class Dummy
{
	int  n;
	int k = 18;
	int m[10];
public:
	Dummy() {n=15;}
	void f1( bool ok ) { printf( "f1(): n = %d; k = %d; %s\n", n, k, ok ? "OK" : "FK" ); ++n; }
	void f2() { printf( "f2(): n = %d; k = %d\n", n, k ); }
};

void testWithLambdas()
{
	AllocInitiator ai;
	Collable c;
	Dummy d;
	printf( "about to add1 lambda...\n" );
	c.add1( [&d](bool b) { d.f1(b); printf( "lambda-based solution [1]\n" ); } );
	printf( "about to add2 lambda...\n" );
	c.add2( [&d]() { d.f2(); printf( "lambda-based solution [2]" ); } );
	c.envoke1( true );
	c.envoke1( false );
	c.envoke2();
	printf( "about to clear2 lambda...\n" );
	c.clear2();
	printf( "about to clear1 lambda...\n" );
	c.clear1();
	printf( "...done\n" );

	g_AllocManager.killAllZombies();
/*	g_AllocManager.deinitialize();
	g_AllocManager.disable();*/
}

