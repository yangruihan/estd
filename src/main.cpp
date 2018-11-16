#include <iostream>
#include <string>
#include <exception>
#include "estd/memory_pool.hpp"

using namespace std;

string ASSERT_TRUE(bool condition)
{
    if (condition)
        return "Pass";
    
    throw runtime_error("Failed");
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

    cout << "-- alloc int again" << endl;
    cout << ASSERT_TRUE(i == mem_pool->alloc<int>());
    mem_pool->dump(cout);

    cout << ASSERT_TRUE(*i == 100) << endl;

    cout << "-- alloc int j" << endl;
    auto j = mem_pool->alloc<int>();
    *j = 28;
    mem_pool->dump(cout);
    cout << ASSERT_TRUE((*j + *i) == 128) << endl;
    ASSERT_TRUE((j - i) * sizeof(int) == 2 * mem_pool->block_size());

    cout << "-- alloc int k" << endl;
    auto k = mem_pool->alloc<int>();
    mem_pool->dump(cout);

    mem_pool->free(i);
    mem_pool->free(k);
    mem_pool->dump(cout);

    mem_pool->free(j);
    mem_pool->dump(cout);

    auto arr = mem_pool->alloc_arr<int>(10);
    for (size_t i = 0; i < 10; i++)
        *(arr + i) = i;
    mem_pool->dump(cout);
    for (size_t i = 0; i < 10; i++)
        cout << *(arr + i) << "  ";

    mem_pool->free(arr);
    mem_pool->dump(cout);

    int pause;
    cin >> pause;

    delete mem_pool;
}
