#include "foobar.hpp"

#include <iostream>
#include <ostream>

int main(int argc, char **argv) {
  std::cout << "Hello-from-private-main" << std::endl;
  std::cout << foobar(3) << std::endl;
}
