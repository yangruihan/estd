#include <iostream>
#include <string>
#include "memory_pool.hpp"

using namespace std;

string ASSERT_TRUE(bool condition)
{
    if (condition)
        return "Pass";
    
    return "Failed";
}

int main()
{
    const auto mem_pool = new estd::memory_pool<>();
    mem_pool->dump(cout);

    cout << "-- alloc int" << endl;
    auto i = mem_pool->alloc<int>();
    *i = 100;
    cout << ASSERT_TRUE(*i == 100) << endl;
    mem_pool->dump(cout);

    cout << "-- free int" << endl;
    mem_pool->free(i);
    mem_pool->free(i);
    mem_pool->free<int>(nullptr);
    mem_pool->dump(cout);

    int pause;
    cin >> pause;

    delete mem_pool;
}
