#include "../hstring.h"

#include <iostream>


using namespace std;
using namespace fool;


int main()
{
    const char *p1 = "hello";
    hstring_core s1(p1,5);
    const char *p2 = s1.data();
    cout << p2 << endl;

}