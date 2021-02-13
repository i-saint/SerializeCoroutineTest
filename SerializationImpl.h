#pragma once
#include <string>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <algorithm>
#include "Serialization.h"

#define sgEnableIf(...) std::enable_if_t<__VA_ARGS__, bool> = true

namespace sg {

using creator_t = void* (*)();
void register_type(const char* name, creator_t c);

void* create_instance_(const char* name);
template<class T> T* create_instance(const char* name) { return static_cast<T*>(create_instance_(name)); }

template<class T>
struct type_registrar
{
    static void* create() { return new T(); }

    type_registrar()
    {
        register_type(typeid(T).name(), &create);
    }
};



// POD
template<class T, sgEnableIf(serializable_pod<T>::value)>
inline void write(serializer& s, const T& v)
{
    s.write(&v, sizeof(T));
}
template<class T, sgEnableIf(serializable_pod<T>::value)>
inline void read(deserializer& d, T& v)
{
    d.read(&v, sizeof(T));
}


// serializable object
template<class T, sgEnableIf(serializable<T>::value)>
inline void write(serializer& s, const T& v)
{
    serializable<T>::serialize(s, v);
}
template<class T, sgEnableIf(serializable<T>::value)>
inline void read(deserializer& d, T& v)
{
    serializable<T>::deserialize(d, v);
}


// serializable pointer
template<class T, sgEnableIf(serializable<T>::value)>
inline hptr write(serializer& s, T* const& v)
{
    hptr handle = s.getHandle(v);
    write(s, handle);
    if (handle.isFlesh()) {
        // write type name
        const char* type_name = typeid(*v).name();
        uint32_t name_len = (uint32_t)std::strlen(type_name);
        write(s, name_len);
        s.write(type_name, name_len);

        // write data
        write(s, *v);
    }
    return handle;
}
template<class T, sgEnableIf(serializable<T>::value)>
inline hptr read(deserializer& d, T*& v)
{
    hptr handle;
    read(d, handle);
    if (handle.isFlesh()) {
        // read type name and create instance
        uint32_t name_len;
        read(d, name_len);

        auto read_name = [&d, name_len](char* name) {
            d.read(name, name_len);
            name[name_len] = '\0';
        };

        if (name_len < 1024) {
            char name[1024];
            read_name(name);
            v = create_instance<T>(name);
        }
        else {
            char *name = new char[name_len + 1];
            read_name(name);
            v = create_instance<T>(name);
            delete[] name;
        }

        // register pointer
        d.setPointer(handle, v);

        // deserialize instance
        read(d, *v);
    }
    else {
        d.getPointer(handle, v);
    }
    return handle;
}



// POD fixed array
template<class T, uint32_t N, sgEnableIf(serializable_pod<T>::value)>
inline void write(serializer& s, const T(&v)[N])
{
    s.write(v, sizeof(T) * N);
}
template<class T, uint32_t N, sgEnableIf(serializable_pod<T>::value)>
inline void read(deserializer& d, T(&v)[N])
{
    d.read(v, sizeof(T) * N);
}

// object fixed array
template<class T, size_t N, sgEnableIf(serializable<T>::value)>
inline void write(serializer& s, const T(&v)[N])
{
    for (size_t i = 0; i < N; ++i)
        s.write(v[i]);
}
template<class T, size_t N, sgEnableIf(serializable<T>::value)>
inline void read(deserializer& d, T(&v)[N])
{
    for (size_t i = 0; i < N; ++i)
        d.read(v[i]);
}

// pointer fixed array
template<class T, size_t N, sgEnableIf(serializable<T>::value)>
static void write(serializer& s, const T* (&v)[N])
{
    for (size_t i = 0; i < N; ++i)
        s.write(v[i]);
}
template<class T, size_t N, sgEnableIf(serializable<T>::value)>
static void read(deserializer& d, T* (&v)[N])
{
    for (size_t i = 0; i < N; ++i)
        d.read(v[i]);
}

// POD array
template<class T, sgEnableIf(serializable_pod<T>::value)>
inline void write_array(serializer& s, const T* v, uint32_t n)
{
    s.write(v, sizeof(T) * n);
}
template<class T, sgEnableIf(serializable_pod<T>::value)>
inline void read_array(deserializer& d, T* v, uint32_t n)
{
    d.read(v, sizeof(T) * n);
}

// object array
template<class T, sgEnableIf(serializable<T>::value)>
static void write_array(serializer& s, const T* v, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        write(s, v[i]);
}
template<class T, sgEnableIf(serializable<T>::value)>
static void read_array(deserializer& d, T* v, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        read(d, v[i]);
}

// pointer array
template<class T, sgEnableIf(serializable<T>::value)>
static void write_array(serializer& s, T* const* v, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        write(s, v[i]);
}
template<class T, sgEnableIf(serializable<T>::value)>
static void read_array(deserializer& d, T** v, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        read(d, v[i]);
}


// specializations

template<class T>
struct serializable<std::unique_ptr<T>> : serialize_nonintrusive<T>
{
    static void serialize(serializer& s, const std::unique_ptr<T>& v)
    {
        write(s, v.get());
    }

    static void deserialize(deserializer& d, std::unique_ptr<T>& v)
    {
        T* p;
        read(d, p);
        v.reset(p);
    }
};

template<class T>
struct serializable<std::shared_ptr<T>> : serialize_nonintrusive<T>
{
    static void serialize(serializer& s, const std::shared_ptr<T>& v)
    {
        write(s, v.get());
    }

    static void deserialize(deserializer& d, std::shared_ptr<T>& v)
    {
        T* p;
        hptr handle = read(d, p);
        if (p) {
            auto& rec = d.getRecord(handle);
            if (!rec.shared) {
                v.reset(p);
                rec.shared = v;
            }
            else {
                v = std::static_pointer_cast<T>(rec.shared);
            }
        }
#ifdef sgDebug
        else if(!handle.isNull()) {
            mu::DbgBreak();
        }
#endif
    }
};

template<class T>
struct serializable<std::basic_string<T>> : serialize_nonintrusive<std::basic_string<T>>
{
    static void serialize(serializer& s, const std::basic_string<T>& v)
    {
        uint32_t size = (uint32_t)v.size();
        write(s, size);
        write_array(s, v.data(), size);
    }

    static void deserialize(deserializer& d, std::basic_string<T>& v)
    {
        uint32_t size;
        read(d, size);
        v.resize(size);
        read_array(d, (T*)v.data(), size);
    }
};


// std::vector
template<class T>
struct serializable<std::vector<T>> : serialize_nonintrusive<std::vector<T>>
{
    static void serialize(serializer& s, const std::vector<T>& v)
    {
        uint32_t size = (uint32_t)v.size();
        write(s, size);
        write_array(s, v.data(), size);
    }

    static void deserialize(deserializer& d, std::vector<T>& v)
    {
        uint32_t size;
        read(d, size);
        v.resize(size);
        read_array(d, v.data(), size);
    }
};

// std::list
template<class T>
struct serializable<std::list<T>> : serialize_nonintrusive<std::list<T>>
{
    static void serialize(serializer& s, const std::list<T>& v)
    {
        uint32_t size = (uint32_t)v.size();
        write(s, size);
        for (const auto& e : v)
            write(s, e);
    }

    static void deserialize(deserializer& d, std::list<T>& v)
    {
        uint32_t size;
        read(d, size);
        v.resize(size);
        for (auto& e : v)
            read(d, e);
    }
};

// std::set
template<class T>
struct serializable<std::set<T>> : serialize_nonintrusive<std::set<T>>
{
    static void serialize(serializer& s, const std::set<T>& v)
    {
        uint32_t size = (uint32_t)v.size();
        write(s, size);
        for (const auto& e : v)
            write(s, e);
    }

    static void deserialize(deserializer& d, std::set<T>& v)
    {
        uint32_t size;
        read(d, size);
        for (uint32_t i = 0; i < size; ++i) {
            T value;
            read(d, value);
            v.insert(std::move(value));
        }
    }
};

// std::map
template<class K, class V>
struct serializable<std::map<K, V>> : serialize_nonintrusive<std::map<K, V>>
{
    static void serialize(serializer& s, const std::map<K, V>& v)
    {
        uint32_t size = (uint32_t)v.size();
        write(s, size);
        for (const auto& kvp : v) {
            write(s, kvp.first);
            write(s, kvp.second);
        }
    }

    static void deserialize(deserializer& d, std::map<K, V>& v)
    {
        uint32_t size;
        read(d, size);
        for (uint32_t i = 0; i < size; ++i) {
            K key;
            V value;
            read(d, key);
            read(d, value);
            v.insert(std::make_pair(std::move(key), std::move(value)));
        }
    }
};

} // namespace sg


#define sgConcat2(x,y) x##y
#define sgConcat(x, y) sgConcat2(x, y)
#define sgRegisterType(T) static ::sg::type_registrar<T> sgConcat(g_sg_registrar, __COUNTER__);

#define sgWrite(V) ::sg::write(s, V);
#define sgRead(V) ::sg::read(d, V);
