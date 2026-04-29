/*
 * libs/test — default main wrapper.
 *
 * Consumers link this object only if they want a no-config runner. A
 * consumer that needs custom argv handling, embedding, or pre-test setup
 * can omit this object and call saturn_test_main() from its own main().
 */

#include <saturn_test/test.h>

int main(int argc, char** argv)
{
    return saturn_test_main(argc, argv);
}
