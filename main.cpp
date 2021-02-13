#include "pch.h"

template<class T>
struct generator
{
    struct promise_type;
    using handle = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        T current_value;
        static auto get_return_object_on_allocation_failure() { return generator{ nullptr }; }
        auto get_return_object() { return generator{ handle::from_promise(*this) }; }
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() { return std::suspend_always{}; }
        void unhandled_exception() { std::terminate(); }
        void return_void() {}
        auto yield_value(T value) {
            current_value = value;
            return std::suspend_always{};
        }
    };

    struct iterator
    {
        handle coro;
        bool done;

        iterator& operator++()
        {
            coro.resume();
            done = coro.done();
            return *this;
        }
        bool operator!=(const iterator& rhs) const { return done != rhs.done; }
        T operator*() const { return coro.promise().current_value; }
    };

    generator(generator const&) = delete;
    generator(generator&& rhs) : m_handle(rhs.m_handle) { rhs.m_handle = nullptr; }
    ~generator() { if (m_handle) m_handle.destroy(); }

    bool move_next() { return m_handle ? (m_handle.resume(), !m_handle.done()) : false; }
    T current_value() { return m_handle.promise().current_value; }
    iterator begin() { m_handle.resume(); return { m_handle, m_handle.done() }; }
    iterator end() { return { {}, true }; }

    void serialize(std::ostream& os);
    void deserialize(std::istream& os);

private:
    generator(handle h) : m_handle(h) {}
    handle m_handle;
};


struct ModuleRVA
{
    std::string module_name{};
    size_t offset{};
};

ModuleRVA GetRVA(void* addr)
{
    HMODULE mod{};
    char buf[1024]{};
    ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)addr, &mod);
    ::GetModuleFileNameA(mod, buf, std::size(buf));
    return { std::strrchr(buf, '\\') + 1, (size_t)addr - (size_t)mod };
}

template<class T>
void generator<T>::serialize(std::ostream& os)
{
    auto* ptr = (byte*)m_handle.address();

    size_t frame_size = ::HeapSize(::GetProcessHeap(), 0, ptr);
    void* frame_end = ptr + frame_size;
    void* resume_addr = *(void**)(ptr); ptr += sizeof(void*);
    void* destroy_addr = *(void**)(ptr); ptr += sizeof(void*);

    HMODULE mod{};
    char buf[1024]{};
    ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)resume_addr, &mod);
    ::GetModuleFileNameA(mod, buf, std::size(buf));

    std::string module_name{ std::strrchr(buf, '\\') + 1 };
    uint32_t module_len = uint32_t(module_name.size());
    os.write((char*)&module_len, sizeof(uint32_t));
    os.write(module_name.c_str(), module_len);

    uint32_t resume_rva = uint32_t((size_t)resume_addr - (size_t)mod);
    uint32_t destroy_rva = uint32_t((size_t)destroy_addr - (size_t)mod);
    os.write((char*)&resume_rva, sizeof(uint32_t));
    os.write((char*)&destroy_rva, sizeof(uint32_t));

    uint32_t data_size = uint32_t(size_t(frame_end) - size_t(ptr));
    os.write((char*)&data_size, sizeof(uint32_t));
    os.write((char*)ptr, data_size);
}

template<class T>
void generator<T>::deserialize(std::istream& is)
{
    auto* ptr = (byte*)m_handle.address();
    void** resume_addr = (void**)(ptr); ptr += sizeof(void*);
    void** destroy_addr = (void**)(ptr); ptr += sizeof(void*);

    std::string module_name;
    uint32_t module_len;
    is.read((char*)&module_len, sizeof(uint32_t));
    module_name.resize(module_len);
    is.read(module_name.data(), module_len);

    HMODULE hmod = ::GetModuleHandleA(module_name.c_str());
    uint32_t resume_rva;
    uint32_t destroy_rva;
    is.read((char*)&resume_rva, sizeof(uint32_t));
    is.read((char*)&destroy_rva, sizeof(uint32_t));
    *resume_addr = (byte*)hmod + resume_rva;
    *destroy_addr = (byte*)hmod + destroy_rva;

    uint32_t data_size;
    is.read((char*)&data_size, sizeof(uint32_t));
    is.read((char*)ptr, data_size);
}


generator<int> iota(int v)
{
    for (int i = 0; i < v; ++i) {
        co_yield i;
    }
}

int main(int argc, char** argv)
{
    auto g = iota(10);

    if (argc >= 2) {
        std::fstream fin(argv[1], std::ios::in | std::ios::binary);
        if (fin) {
            g.deserialize(fin);
        }
    }
    else {
        std::fstream fout("data.bin", std::ios::out | std::ios::binary);

        g.move_next();
        g.move_next();
        g.move_next();
        g.serialize(fout);
    }

    for (auto i : g) {
        printf("%d\n", i);
    }
}
