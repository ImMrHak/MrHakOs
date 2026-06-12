/*
 * Build-only C++ example for MrHakOS.
 *
 * Keep this compatible with the kernel's freestanding C++ constraints:
 * no exceptions, no RTTI, no iostreams, and no hosted standard library use.
 */

class Counter {
public:
    explicit Counter(int start) : value(start) {}

    void add(int amount) {
        value += amount;
    }

    int get() const {
        return value;
    }

private:
    int value;
};

extern "C" int mrhakos_cpp_hello_status() {
    Counter counter(0);
    counter.add(2);
    counter.add(4);
    return counter.get() == 6 ? 0 : 1;
}

