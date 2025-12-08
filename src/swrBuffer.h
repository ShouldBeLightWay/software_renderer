#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace swr
{
    // Forward decl device class
    class Device;

    // Буфер ресурсов (вершинный буфер, индексный буфер и т.д.)
    class Buffer
    {
        // Поскольку созданием буфера занимается только устройство, конструктор приватный
      private:
        Buffer( size_t elementSize, size_t elementCount )
            : elemSize( elementSize ), elemCount( elementCount ), dataVec( elementSize * elementCount )
        {
        }
        friend class Device; // Разрешить Device создавать Buffer

      public:
        void *data()
        {
            return dataVec.data();
        }

        const void *data() const
        {
            return dataVec.data();
        }

        size_t elementSize() const
        {
            return elemSize;
        }

        size_t elementCount() const
        {
            return elemCount;
        }

      private:
        size_t elemSize;
        size_t elemCount;
        std::vector<uint8_t> dataVec;
    };
} // namespace swr