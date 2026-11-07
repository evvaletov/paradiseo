#include <eo>
#include <iostream>
#include <stdexcept>

namespace {
struct Interface {
    virtual int value() const = 0;
    virtual ~Interface() = default;
};

struct DefaultOperator : Interface {
    int value() const override { return 42; }
};

struct ArgumentOperator : Interface {
    explicit ArgumentOperator(int value) : value_(value) {}
    int value() const override { return value_; }
    int value_;
};

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class Factory>
void checkSharedCache(Factory& factory, int value)
{
    auto first = factory.instantiate_ptr(false);
    require(first && first->value() == value, "shared construction failed");
    require(factory.instantiate_ptr(false) == first, "shared cache was not reused");

    auto& raw = factory.instantiate(false);
    require(raw.value() == value, "raw construction failed after shared construction");
    require(factory.instantiate_ptr(false) == first, "raw construction changed shared cache");
    require(&factory.instantiate(false) == &raw, "raw cache was not reused");

    auto fresh = factory.instantiate_ptr();
    require(fresh && fresh != first, "default shared call did not create a fresh object");
    require(first->value() == value, "replacing shared cache invalidated a retained pointer");
    require(factory.instantiate_ptr(false) == fresh, "fresh shared object was not cached");
    require(&factory.instantiate(false) == &raw, "shared construction changed raw cache");
}

template<class Factory>
void checkRawFirst(Factory& factory, int value)
{
    auto& raw = factory.instantiate(false);
    auto shared = factory.instantiate_ptr(false);
    require(shared && shared->value() == value, "shared call after raw call returned null");
    require(shared.get() != &raw, "raw and shared ownership must remain separate");
    require(factory.instantiate_ptr(false) == shared, "mixed-use shared cache was not reused");
}
}

int main()
{
    try {
        eoForgeOperator<Interface, DefaultOperator> defaultSharedFirst;
        checkSharedCache(defaultSharedFirst, 42);
        eoForgeOperator<Interface, DefaultOperator> defaultRawFirst;
        checkRawFirst(defaultRawFirst, 42);

        eoForgeOperator<Interface, ArgumentOperator, int> argsSharedFirst(7);
        checkSharedCache(argsSharedFirst, 7);
        eoForgeOperator<Interface, ArgumentOperator, int> argsRawFirst(9);
        checkRawFirst(argsRawFirst, 9);

        eoForgeMap<Interface> factories(false);
        factories.add<ArgumentOperator>("op", 1);
        require(factories.instantiate("op").value() == 1, "map construction failed");
        factories.setup<ArgumentOperator>("op", 2);
        require(factories.instantiate("op").value() == 2, "map setup with arguments failed");
        factories.setup<DefaultOperator>("op");
        require(factories.instantiate("op").value() == 42, "map setup without arguments failed");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
