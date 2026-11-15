#include "foo.hpp"
#include "bar.hpp"

foo_t foobar(int x) {
  return foo(bar(x));
}
