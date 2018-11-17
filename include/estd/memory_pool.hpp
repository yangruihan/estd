#pragma once

#include <forward_list>
#include <cstdarg>
#include <string>

namespace estd
{
    
    static std::string format_str(const char* format, ...)
    {
        char str_buffer[256];
        va_list args;
        va_start(args, format);
        std::vsnprintf(str_buffer, 256, format, args);
        va_end(args);
        return std::string(str_buffer);
    }

    static const size_t C_DEFAULT_POOL_SIZE = 4096; // Default pool size is 4kb

    enum block_flag : uint64_t
    {
        FREE = 0,
        USING = 1,
    };

    
    /**
    * memory block struct
    * size         8 Bytes
    * block_flag   8 Bytes
    * prev         8 Bytes
    * next         8 Bytes
    * sum         32 Bytes
    *
    */
    struct block
    {
        uint64_t    size;   // data size, (total_size / BLOCK_SIZE)
        block_flag  flag;   // falg for block

        union {
            block*      prev;   // pointer to prev block
            uint64_t    __p;
        };

        union {
            block*      next;   // pointer to next block
            uint64_t    __n;
        };
    };

    static const size_t BLOCK_SIZE      = sizeof(block);
    static const size_t BLOCK_SIZE_MASK = BLOCK_SIZE - 1;
    
    
    /**
    * Default Allocator 
    * use **new** and **delete** key word
    *
    */
    class default_allocator
    {
    public:
        template<class T>
        static T* alloc() { return new T; }

        template<class T>
        static T* alloc_arr(const size_t& size) { return new T[size]; }

        template<class T, class ... TArgs>
        static T* alloc_args(const TArgs &&... args)
        {
            return new T(std::forward(args)...);
        }

        template<class T, class ... TArgs>
        static T* alloc_arr_args(const size_t& size, const TArgs &&... args)
        {
            auto p = new T[size];
            for (size_t i = 0; i < size; i++)
                p[i] = new T(std::forward(args)...);
            return p;
        }

        template<class T>
        static bool free(T* t)
        {
            delete t;
            return true;
        }

        template<class T>
        static bool free_arr(T* t)
        {
            delete[] t;
            return true;
        }
    };

    
    /**
     * Legacy memory pool
     * default use default_allocator and 4kb size
     * 
     */
    template<class Allocator = default_allocator, size_t DefaultSize = C_DEFAULT_POOL_SIZE>
    class legacy_memory_pool
    {
    public:
        const size_t ALLOC_BLOCK_COUNT  = DefaultSize / BLOCK_SIZE;
        const size_t ALLOC_SIZE         = DefaultSize;

        legacy_memory_pool() { _create(); }
        virtual ~legacy_memory_pool() { _destroy();  }

        template<class T>
        T* alloc()
        {
            return static_cast<T*>(_alloc(sizeof(T)));
        }

        template<class T>
        T* alloc_arr(const size_t& count)
        {
            return static_cast<T*>(_alloc(count * sizeof(T)));
        }

        template<class T, class ...TArgs>
        T* alloc_args(const TArgs &&... args)
        {
            T* obj = static_cast<T*>(_alloc(sizeof(T)));
            (*obj)(std::forward(args)...);
            return obj;
        }

        template<class T, class ...TArgs>
        T* alloc_arr_args(const size_t& count, const TArgs &&... args)
        {
            T* obj = static_cast<T*>(_alloc(count * sizeof(T)));
            for (size_t i = 0; i < count; i++)
                (obj[i])(std::forward(args)...);
            return obj;
        }

        template<class T, class Y>
        Y* realloc(T* obj)
        {
            return static_cast<Y*>(_realloc(obj, sizeof(Y)));
        }

        template<class T>
        bool free(T* obj)
        {
            return _free(obj);
        }

        template<class T>
        bool free_arr(T* obj)
        {
            return _free(obj);
        }

        size_t available() const
        {
            return free_size_ * BLOCK_SIZE;
        }
        
        void clear()
        {
            _init();
        }

        size_t free_size() const
        {
            return BLOCK_SIZE * free_size_;
        }

        size_t free_block_count() const
        {
            return free_size_;
        }

        size_t block_size() const
        {
            return BLOCK_SIZE;
        }

        void dump(std::ostream& os) const
        {
            auto ptr = block_head_;
            os << "\n--------------------------------------------------" << std::endl;
            os << format_str("Memory Info | sum: \t\t%zuBytes - %zu\n", ALLOC_SIZE, ALLOC_BLOCK_COUNT);
            os << format_str("Memory Info | available: \t%zuBytes - %zu\n", free_size_ * BLOCK_SIZE, free_size_);
            os << "\n--------------------------------------------------" << std::endl;
            if (ptr->next == ptr)
            {
                if (_block_get_flag(ptr) == block_flag::USING)
                    _dump_block(ptr, os);
                else
                    os << "Memory Info | All Free." << std::endl;
            }
            else
            {
                _dump_block(ptr, os);
                ptr = ptr->next;
                while (ptr != block_head_)
                {
                    _dump_block(ptr, os);
                    ptr = ptr->next;
                }
            }
            os << "--------------------------------------------------\n" << std::endl;
        }

    private:
        Allocator   allocator_;
        block*      block_head_;
        block*      block_curt_;
        size_t      free_size_;     // total_free_size / BLOCK_SIZE

        static size_t _block_align8(const size_t& size)
        {
            if ((size & BLOCK_SIZE_MASK) == 0)
                return size / BLOCK_SIZE;
            return (size / BLOCK_SIZE) + 1;
        }

        static void _block_init(block* b, const size_t& size)
        {
            b->size = size;
            b->flag = block_flag::FREE;
            b->prev = nullptr;
            b->next = nullptr;
        }

        static void _block_connect(block* curt, block* next)
        {
            next->prev = curt;
            next->next = curt->next;
            next->next->prev = next;
            curt->next = next;
        }

        /**
         *    curt          next
         * -----------    --------
         * | To Free | -> | Free |
         * -----------    --------
         */
        static size_t _block_merge(block* curt, block* next)
        {
            next->next->prev = curt;
            curt->next = next->next;
            curt->size += next->size + 1;
            return curt->size;
        }

        static void _block_set_flag(block* b, const block_flag& flag)
        {
            b->flag = flag;
        }

        static block_flag _block_get_flag(const block *b)
        {
            return b->flag;
        }

        static void _dump_block(block* blk, std::ostream& os)
        {
            os << format_str("Memory Info | [%p-%p] Data Size: %zuBytes - %zu, State: %s\n",
                blk,
                blk + blk->size,
                blk->size * BLOCK_SIZE,
                blk->size,
                _block_get_flag(blk) == block_flag::USING ? "USING" : "FREE");
        }

        void _create()
        {
            block_head_ = allocator_.template alloc_arr<block>(ALLOC_BLOCK_COUNT);
            _init();
        }

        void _init()
        {
            free_size_ = ALLOC_BLOCK_COUNT;
            _block_init(block_head_, free_size_ - 1);
            block_head_->prev = block_head_->next = block_head_;
            block_curt_ = block_head_;
        }

        void _destroy()
        {
            allocator_.free_arr(block_head_);
        }

        bool _check_space(const size_t& size, size_t& aligned_size) const
        {
            // align size with 8Byte, and div by BLOCK_SIZE
            aligned_size = _block_align8(size);
            return aligned_size < free_size_;
        }

        void* _alloc(const size_t& size)
        {
            if (size == 0)
                return nullptr;

            size_t aligned_size;
            if (!_check_space(size, aligned_size))
                return nullptr;

            auto blk = block_curt_;
            do
            {
                if (_block_get_flag(blk) == block_flag::FREE
                    && blk->size >= aligned_size)
                {
                    block_curt_ = blk;
                    return _alloc_free_block(aligned_size);
                }

                blk = blk->next;
            } while (blk != block_curt_);

            return nullptr;
        }

        bool _free(void* p)
        {
            auto blk = static_cast<block*>(p);
            blk--; // get block header
            if (!_verify_address(blk, block_flag::USING))
                return false;

            // just one block
            if (blk->next == blk)
            {
                _block_set_flag(blk, block_flag::FREE);
                return true;
            }

            const auto prev_b = blk->prev;
            const auto next_b = blk->next;
            const auto prev_b_mask = (_block_get_flag(prev_b) == block_flag::FREE) && prev_b < blk;
            const auto next_b_mask = (_block_get_flag(next_b) == block_flag::FREE) && blk < next_b;
            const auto mask = (prev_b_mask << 1) + next_b_mask;

            free_size_ += blk->size + 1;
            switch (mask)
            {
            // prev USING | next USING
            case 0:
                _block_set_flag(blk, block_flag::FREE);
                break;

            // prev USING | next FREE
            case 1:
                if (block_curt_ == next_b)
                    block_curt_ = blk;
                _block_merge(blk, next_b);
                _block_set_flag(blk, block_flag::FREE);
                break;

            // prev FREE | next USING
            case 2:
                if (block_curt_ == blk)
                    block_curt_ = prev_b;
                _block_merge(prev_b, blk);
                break;

            // prev FREE | next FREE
            case 3:
                if (block_curt_ == next_b)
                    block_curt_ = prev_b;
                _block_merge(prev_b, blk);
                _block_merge(prev_b, next_b);
                break;

            default:
                return false;
            }

            return true;
        }

        void* _realloc(void* p, const size_t& new_size)
        {
            auto blk = static_cast<block*>(p);
            blk--; // get block header
            if (!_verify_address(blk, block_flag::USING))
                return nullptr;

            // TODO
            // Opt
            // if new_size < current_size split block
            // if new_size > current_size and next block is free
            //   merge next block and this block
            auto aligned_size = _block_align8(new_size);
            const auto alloc_ret = _alloc(aligned_size);
            if (!alloc_ret)
            {
                _free(blk);
                return nullptr;
            }
            auto old_size = blk->size;
            memmove(alloc_ret, p, BLOCK_SIZE *
                (old_size > aligned_size ? aligned_size : old_size));
            _free(p);
            return alloc_ret;
        }

        void* _alloc_free_block(const size_t& size)
        {
            if (block_curt_->size == size)
                return _alloc_cur_block(size);

            const auto new_size = block_curt_->size - size - 1;
            const auto new_block = block_curt_ + size + 1;
            _block_init(new_block, new_size);
            _block_connect(block_curt_, new_block);
            return _alloc_cur_block(size);
        }

        void* _alloc_cur_block(const size_t& size)
        {
            _block_set_flag(block_curt_, block_flag::USING);
            block_curt_->size = size;
            free_size_ -= size + 1;
            const auto data = static_cast<void*>(block_curt_ + 1);
            block_curt_ = block_curt_->next;
            return data;
        }

        bool _verify_address(const block* b, const block_flag& flag) const
        {
            if (b < block_head_ || b >= block_head_ + ALLOC_BLOCK_COUNT)
                return false;
            
            return (b->next->prev == b) && (b->prev->next == b) && (_block_get_flag(b) == flag);
        }
    };

    template<size_t DefaultSize = C_DEFAULT_POOL_SIZE>
    using memory_pool = legacy_memory_pool<default_allocator, DefaultSize>;

}
