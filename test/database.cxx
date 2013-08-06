#include "../app/database.h"

#include <cstdint>

int main(int argc, char **argv)
{
	try
	{
		SQLDatabase Database;

		int a = 23;
		bool b = true;
		bool b2 = false;
		uint64_t c = 221;
		std::string d = "all good";
		struct { int whatever, whatever2; } e, e2;
		e.whatever = 88;
		e.whatever2 = 2842967;
		e2.whatever = 2929;
		e2.whatever2 = -222;

		Database.Execute("CREATE TABLE one (a INTEGER, b BOOLEAN, b2 BOOLEAN, c DATETIME, d VARCHAR, e BINARY)");
		auto Insert = Database.Prepare<void(int, bool, bool, uint64_t, std::string, decltype(e))>("INSERT INTO one VALUES (?, ?, ?, ?, ?, ?)");
		(*Insert)(a, b, b2, c, d, e);
		(*Insert)(1, false, true, 90, "hey", e2);

		auto Get = Database.Prepare<std::tuple<int, bool, bool, uint64_t, std::string, decltype(e)>(void)>("SELECT * FROM one");
		int Count = 0;
		Get->Execute([&](int &&GotA, bool &&GotB, bool &&GotB2, uint64_t &&GotC, std::string &&GotD, decltype(e) &&GotE)
		{
			if (Count == 0)
			{
				assert(GotA == a);
				assert(GotB == b);
				assert(GotB2 == b2);
				assert(GotC == c);
				assert(GotD == d);
				assert(memcmp(&GotE, &e, sizeof(e)) == 0);
			}
			else if (Count == 1)
			{
				assert(GotD == "hey");
			}
			++Count;
		});

		auto Got = (*Get)();
		assert(Got);
		assert(std::get<0>(*Got) == a);
		assert(std::get<1>(*Got) == b);
		assert(std::get<2>(*Got) == b2);
		assert(std::get<3>(*Got) == c);
		assert(std::get<4>(*Got) == d);
		assert(memcmp(&std::get<5>(*Got), &e, sizeof(e)) == 0);

		auto Get2 = Database.Prepare<int(void)>("SELECT a FROM one");
		assert(*(*Get2)() == 23);
	}
	catch (SystemError &Error) { std::cerr << Error << std::endl; return 1; }

	return 0;
}

