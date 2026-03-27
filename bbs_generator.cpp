#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <windows.h>

// ─────────────────────────────────────────────
//  Вспомогательный класс-обёртка над BIGNUM
//  (автоматически освобождает память)
// ─────────────────────────────────────────────
struct BN {
    BIGNUM* n;
    BN()              : n(BN_new())       { if (!n) throw std::runtime_error("BN_new"); }
    explicit BN(int v): n(BN_new())       { BN_set_word(n, v); }
    ~BN()                                 { BN_free(n); }
    operator BIGNUM*() const              { return n; }
    BIGNUM** addr()                       { return &n; }
};

// ─────────────────────────────────────────────
//  Класс генератора BBS
// ─────────────────────────────────────────────
class BlumBlumShub {
public:
    // bits_size — размер простых чисел p и q в битах (минимум 512)
    explicit BlumBlumShub(int bits_size = 512) {
        std::cout << "[*] Generation prime numbers (" << bits_size << " bit)...\n";
        generate_blum_prime(p_, bits_size);
        generate_blum_prime(q_, bits_size);

        // n = p * q  (модуль Блюма)
        BN_CTX* ctx = BN_CTX_new();
        BN_mul(n_, p_, q_, ctx);
        BN_CTX_free(ctx);

        std::cout << "[*] Module N is generated (" << BN_num_bits(n_) << " bit)\n";

        // Генерация случайного seed: 1 < seed < n, gcd(seed, n) = 1
        generate_seed();
        std::cout << "[*] Seed is set\n";

        // Первый шаг: x_0 = seed^2 mod n  (не используется как выход)
        BN_CTX* ctx2 = BN_CTX_new();
        BN_mod_sqr(x_, seed_, n_, ctx2);
        BN_CTX_free(ctx2);
    }

    // Генерирует следующий бит BBS
    int next_bit() {
        BN_CTX* ctx = BN_CTX_new();
        BN_mod_sqr(x_, x_, n_, ctx);   // x = x^2 mod n
        BN_CTX_free(ctx);
        return BN_is_odd(x_);           // младший бит
    }

    // Генерирует `count` байт и записывает их в буфер
    void generate_bytes(unsigned char* buf, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            unsigned char byte = 0;
            for (int bit = 7; bit >= 0; --bit)
                byte |= (next_bit() << bit);
            buf[i] = byte;
        }
    }

private:
    BN p_, q_, n_, seed_, x_;

    // Генерирует простое p такое, что p ≡ 3 (mod 4)
    void generate_blum_prime(BIGNUM* out, int bits) {
        BN_CTX* ctx = BN_CTX_new();
        BN rem, mod4;
        BN_set_word(mod4, 4);
        BN_set_word(rem,  3);

        while (true) {
            BN_generate_prime_ex(out, bits, 0, nullptr, nullptr, nullptr);
            BN tmp;
            BN_nnmod(tmp, out, mod4, ctx);          // tmp = p mod 4
            if (BN_cmp(tmp, rem) == 0) break;       // p ≡ 3 (mod 4) ✓
        }
        BN_CTX_free(ctx);
    }

    // Случайный seed взаимно простой с n
    void generate_seed() {
        BN_CTX* ctx = BN_CTX_new();
        BN gcd, one;
        BN_set_word(one, 1);

        while (true) {
            // Случайное число того же размера, что n
            BN_rand_range(seed_, n_);
            if (BN_is_zero(seed_) || BN_is_one(seed_)) continue;

            BN_gcd(gcd, seed_, n_, ctx);
            if (BN_cmp(gcd, one) == 0) break;      // gcd(seed, n) = 1 ✓
        }
        BN_CTX_free(ctx);
    }
};

// ─────────────────────────────────────────────
//  Точка входа
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {

    SetConsoleOutputCP(CP_UTF8);  // Устанавливает UTF-8 для вывода
    SetConsoleCP(CP_UTF8);        // Устанавливает UTF-8 для ввода

    // Параметры по умолчанию
    std::string filename  = "random_bits.bin";
    size_t      num_bytes = 1024;   // 1 КБ = 8192 бит
    int         key_bits  = 512;    // размер простых чисел

    // Разбор аргументов командной строки
    // Использование: bbs_generator <имя_файла> <байт> <бит_ключа>
    if (argc >= 2) filename  = argv[1];
    if (argc >= 3) num_bytes = std::stoull(argv[2]);
    if (argc >= 4) key_bits  = std::stoi(argv[3]);

    std::cout << "╔══════════════════════════════════╗\n";
    std::cout << "║    Random Bits Generator BBS     ║\n";
    std::cout << "╚══════════════════════════════════╝\n\n";
    std::cout << "  File:        " << filename  << "\n";
    std::cout << "  Byte:        " << num_bytes << "\n";
    std::cout << "  Bit (p,q):   " << key_bits  << "\n\n";

    try {
        BlumBlumShub bbs(key_bits);

        std::cout << "[*] Generation of " << num_bytes << " bytes...\n";

        std::vector<unsigned char> buffer(num_bytes);
        bbs.generate_bytes(buffer.data(), num_bytes);

        // Запись в бинарный файл
        std::ofstream out(filename, std::ios::binary);
        if (!out) throw std::runtime_error("Can't to open the file: " + filename);
        out.write(reinterpret_cast<char*>(buffer.data()), num_bytes);
        out.close();

        std::cout << "[+] Successful! File is saved: " << filename << "\n";
        std::cout << "    Size: " << num_bytes << " bytes ("
                  << num_bytes * 8 << " bit)\n";

    } catch (const std::exception& e) {
        std::cerr << "[-] ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}