#pragma once

#include <utility>
#include <cstdarg>
#include <string>
#include <inttypes.h>

namespace estd
{
    typedef std::string (*dump_obj_handler)(const void* obj);

    static inline std::string format_str(const char* format, ...)
    {
        char str_buffer[256];
        va_list args;
        va_start(args, format);
        std::vsnprintf(str_buffer, 256, format, args);
        va_end(args);
        return std::string(str_buffer);
    }

    static const size_t C_DEFAULT_POOL_SIZE = 4096;         // Default pool size is 4kB
    static const uint32_t C_MAGIC_NUMBER    = 0x6ebf2966;   // magic number

    enum block_flag : uint32_t
    {
        FREE = 0,
        USING = 1,
    };

    
    /**
    * memory block struct
    * size         8 Bytes
    * block_flag   4 Bytes
    * magic_number 4 Bytes
    * prev         8 Bytes
    * next         8 Bytes
    * sum         32 Bytes
    *
    */
    struct block
    {
        uint64_t    size;       // data size, (total_size / BLOCK_SIZE)
        block_flag  flag;       // falg for block
        uint32_t    magic_num;  // magic number for verify

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
    static const size_t BLOCK_SIZE_MASK = 0x7;
    
    
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
        static T* alloc_args(TArgs&& ... args)
        {
            return new T(std::forward<TArgs>(args)...);
        }

        template<class T, class ... TArgs>
        static T* alloc_arr_args(const size_t& size, TArgs&& ... args)
        {
            auto p = new T[size];
            for (size_t i = 0; i < size; i++)
                p[i] = new T(std::forward<TArgs>(args)...);
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
        const size_t ALLOC_SIZE = DefaultSize;

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
        T* alloc_args(TArgs&& ... args)
        {
            T* obj = static_cast<T*>(_alloc(sizeof(T)));
            new(obj) T(std::forward<TArgs>(args)...);
            return obj;
        }

        template<class T, class ...TArgs>
        T* alloc_arr_args(const size_t& count, TArgs&& ... args)
        {
            T* obj = static_cast<T*>(_alloc(count * sizeof(T)));
            for (size_t i = 0; i < count; i++)
                new (obj + i) T(std::forward<TArgs>(args)...);
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

        void clear()
        {
            _init();
        }

        /**
         * check rest space is enough
         * 
         */
        bool check_space(const size_t& size, const bool& prepare_alloc = true)
        {
            return _check_space(size, prepare_alloc);
        }

        /**
         * return all available size
         * (free size) - (block size)
         * 
         */
        size_t available_size() const
        {
            return free_size_ <= BLOCK_SIZE ? 0 : free_size_ - BLOCK_SIZE;
        }

        /**
         * return all free size
         * 
         */
        size_t free_size() const
        {
            return free_size_;
        }

        void dump(std::ostream& os, dump_obj_handler dump_obj_handler = nullptr) const
        {
            auto ptr = block_head_;
            os << "\n----------------------------------------------------------------------------------------" << std::endl;
            os << format_str("- Memory | sum: \t%zuB\n", ALLOC_SIZE);
            os << format_str("- Memory | free: \t%zuB\n", free_size());
            os << format_str("- Memory | available: \t%zuB\n", available_size());
            os << format_str("- Memory | %p-%p\n", block_head_, (char*)block_head_ + ALLOC_SIZE);
            os << "----------------------------------------------------------------------------------------" << std::endl;
            if (ptr->next == ptr)
            {
                if (_block_get_flag(ptr) == block_flag::USING)
                {
                    _dump_block(ptr, os, dump_obj_handler);
                }
                else
                {
                    os << "- Memory | - All Free -" << std::endl;
                    _dump_block(ptr, os);
                }
            }
            else
            {
                _dump_block(ptr, os, dump_obj_handler);
                ptr = ptr->next;
                while (ptr != block_head_)
                {
                    _dump_block(ptr, os, dump_obj_handler);
                    ptr = ptr->next;
                }
            }
            os << "----------------------------------------------------------------------------------------\n" << std::endl;
        }

    private:
        Allocator   allocator_;         // allocator, default use default_allocator
        block*      block_head_;        // the pointer to head block
        block*      block_curt_;        // the pointer to current block, speed up search
        size_t      free_size_;         // total free size (avaiable size + lock size)

        static size_t _block_align8(const size_t& size)
        {
            if ((size & BLOCK_SIZE_MASK) == 0)
                return size;
            return ((size >> 3) + 1) << 3;
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
            curt->size += next->size + BLOCK_SIZE;
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

        static void _dump_block(block* blk, std::ostream& os, dump_obj_handler dump_obj_handler = nullptr)
        {
            const auto blk_flag = _block_get_flag(blk);
            os << format_str("- Memory | %p-%p | Total %4" PRIu64 "B | Header %2zuB | Data %4" PRIu64 "B | %s\n",
                             blk,
                             (char*)(blk + 1) + blk->size,
                             (blk->size + BLOCK_SIZE),
                             BLOCK_SIZE,
                             blk->size,
                             blk_flag == block_flag::USING ? "USING" : "FREE");
            
            if (dump_obj_handler != nullptr && blk_flag == block_flag::USING)
                os << format_str("-- Info | %s\n", dump_obj_handler(static_cast<void*>(blk + 1)).c_str());
        }

        void _create()
        {
            block_head_ = (block*)allocator_.template alloc_arr<char>(ALLOC_SIZE);
            _init();
        }

        void _init()
        {
            free_size_ = ALLOC_SIZE;
            _block_init(block_head_, free_size_ - BLOCK_SIZE);
            block_head_->prev = block_head_->next = block_head_;
            block_curt_ = block_head_;
        }

        void _destroy()
        {
            allocator_.free_arr(block_head_);
        }

        size_t _block_align8_with_check_space(const size_t& size) const
        {
            // align size with 8Byte
            const auto aligned_size = _block_align8(size);
            return aligned_size + BLOCK_SIZE <= free_size_ ? aligned_size : 0;
        }

        bool _check_space(const size_t& size, const bool& prepare_alloc = true)
        {
            // align size with 8Byte
            const auto aligned_size = _block_align8(size);
            if (aligned_size + BLOCK_SIZE > free_size_)
                return false;
            
            auto blk = block_curt_;
            do
            {
                if (_block_get_flag(blk) == block_flag::FREE
                    && blk->size >= aligned_size)
                {
                    if (prepare_alloc)
                        block_curt_ = blk;
                    
                    return true;
                }

                blk = blk->next;
            } while (blk != block_curt_);

            return false;
        }

        void* _alloc(const size_t& size)
        {
            if (size == 0)
                return nullptr;

            const size_t aligned_data_size = _block_align8_with_check_space(size);
            if (aligned_data_size == 0)
                return nullptr;

            auto blk = block_curt_;
            do
            {
                if (_block_get_flag(blk) == block_flag::FREE
                    && blk->size >= aligned_data_size)
                {
                    block_curt_ = blk;
                    return _alloc_free_block(aligned_data_size);
                }

                blk = blk->next;
            } while (blk != block_curt_);

            return nullptr;
        }

        bool _free(void* p)
        {
            auto blk = static_cast<block*>(p);
            // get block header
            blk--;
            
            if (!_verify_address(blk, block_flag::USING))
                return false;

            free_size_ += blk->size + BLOCK_SIZE;

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

            switch (mask)
            {
            // prev USING | next USING
            case 0:
                if (block_curt_ > blk)
                    block_curt_ = blk;
                _block_set_flag(blk, block_flag::FREE);
                break;

            // prev USING | next FREE
            case 1:
                if (block_curt_ > blk)
                    block_curt_ = blk;
                _block_merge(blk, next_b);
                _block_set_flag(blk, block_flag::FREE);
                break;

            // prev FREE | next USING
            case 2:
                if (block_curt_ > prev_b)
                    block_curt_ = prev_b;
                _block_merge(prev_b, blk);
                break;

            // prev FREE | next FREE
            case 3:
                if (block_curt_ > prev_b)
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
            memmove(alloc_ret, p, (old_size > aligned_size ? aligned_size : old_size));
            _free(p);
            return alloc_ret;
        }

        /**
         * alloc free block with size
         * size = aligned data size
         *
         */
        void* _alloc_free_block(const size_t& size)
        {
            // just enough or cann't split new block
            if (block_curt_->size == size
                || block_curt_->size <= size + BLOCK_SIZE)
                return _alloc_cur_block(block_curt_->size);

            const auto new_size = block_curt_->size - size - BLOCK_SIZE;
            const auto new_block = (block*)((char*)block_curt_ + size + BLOCK_SIZE);
            _block_init(new_block, new_size);
            _block_connect(block_curt_, new_block);
            return _alloc_cur_block(size);
        }

        /**
         * alloc current block with size
         * size = aligned data size
         *
         */
        void* _alloc_cur_block(const size_t& size)
        {
            _block_set_flag(block_curt_, block_flag::USING);
            block_curt_->size = size;
            block_curt_->magic_num = C_MAGIC_NUMBER;
            free_size_ -= size + BLOCK_SIZE;
            const auto data = static_cast<void*>(block_curt_ + 1);
            block_curt_ = block_curt_->next;
            return data;
        }

        bool _verify_address(const block* b, const block_flag& flag) const
        {
            if (b < block_head_ || (char*)b >= (char*)block_head_ + ALLOC_SIZE)
                return false;

            if (b->magic_num != C_MAGIC_NUMBER)
                return false;

            return (b->next->prev == b) && (b->prev->next == b) && (_block_get_flag(b) == flag);
        }
    };

    template<size_t DefaultSize = C_DEFAULT_POOL_SIZE>
    using memory_pool = legacy_memory_pool<default_allocator, DefaultSize>;

}
