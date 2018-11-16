#include <iostream>
#include "memory_pool.hpp"

using namespace std;

int main()
{
    const auto mem_pool = new estd::memory_pool<>();
    mem_pool->dump(cout);

    int pause;
    cin >> pause;
}
