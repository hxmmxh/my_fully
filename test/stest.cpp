#include "../hstring.h"

#include <iostream>
#include <cstring>


using namespace std;
using namespace fool;


int main()
{
    const char *p1 = "hello";
    hstring_core s1(p1,5);
    const char *p2 = s1.data();
    hstring_core s2(s1);
    const char *p3 = s2.data();
    char big[1000];
    memset(big,'a', 100);
    hstring_core s3(big,100);
    cout << p2 << " " << p3 << endl;
    const char *p4 = s3.data();
    cout << p4;
}