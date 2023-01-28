#include<bits/stdc++.h>
using namespace std;
int main() {
  for (int i = 0; i < 128; i++) {
    int a = ((i) & 0x88) ? (0) : (1);
    cout << a << " ";
  }
  return 0;
}