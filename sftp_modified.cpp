/*
 * ============================================================
 * MODIFIED S-SFTP IMPLEMENTATION
 * Sender Time & Receiver Time Measured Separately
 * ============================================================
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

using Byte = unsigned char;
const int PORT = 8080;
const int CHUNK_SIZE = 4096;

// ================= CRYPTO =================

Byte AES_KEY[32];
Byte AES_IV[16];

void init_crypto() {
    RAND_bytes(AES_KEY, sizeof(AES_KEY));
    RAND_bytes(AES_IV, sizeof(AES_IV));
}

bool encrypt_data(const std::vector<Byte>& plain, std::vector<Byte>& cipher) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len, cipher_len;

    cipher.resize(plain.size() + 16);

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, AES_KEY, AES_IV);
    EVP_EncryptUpdate(ctx, cipher.data(), &len, plain.data(), plain.size());
    cipher_len = len;
    EVP_EncryptFinal_ex(ctx, cipher.data() + len, &len);
    cipher_len += len;

    cipher.resize(cipher_len);
    EVP_CIPHER_CTX_free(ctx);
    return true;
}

bool decrypt_data(const std::vector<Byte>& cipher, std::vector<Byte>& plain) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len, plain_len;

    plain.resize(cipher.size());

    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, AES_KEY, AES_IV);
    EVP_DecryptUpdate(ctx, plain.data(), &len, cipher.data(), cipher.size());
    plain_len = len;
    EVP_DecryptFinal_ex(ctx, plain.data() + len, &len);
    plain_len += len;

    plain.resize(plain_len);
    EVP_CIPHER_CTX_free(ctx);
    return true;
}

// ================= SENDER =================

void run_sender(const std::string& filename) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(sock, (sockaddr*)&addr, sizeof(addr));

    // READ FILE
    auto start_read = std::chrono::high_resolution_clock::now();
    std::ifstream file(filename, std::ios::binary);
    std::vector<Byte> data((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    auto end_read = std::chrono::high_resolution_clock::now();

    // ENCRYPT
    auto start_encrypt = std::chrono::high_resolution_clock::now();
    std::vector<Byte> encrypted;
    encrypt_data(data, encrypted);
    auto end_encrypt = std::chrono::high_resolution_clock::now();

    // SEND (NETWORK TIME)
    auto start_send = std::chrono::high_resolution_clock::now();
    size_t size = encrypted.size();
    send(sock, &size, sizeof(size), 0);
    send(sock, encrypted.data(), encrypted.size(), 0);
    auto end_send = std::chrono::high_resolution_clock::now();

    close(sock);

    auto read_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_read - start_read).count();
    auto enc_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_encrypt - start_encrypt).count();
    auto send_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_send - start_send).count();

    std::cout << "\n===== SENDER METRICS =====\n";
    std::cout << "File Size (MB): " << std::fixed << std::setprecision(2)
              << data.size() / 1024.0 / 1024.0 << "\n";
    std::cout << "Read Time (ms): " << read_time << "\n";
    std::cout << "Encryption Time (ms): " << enc_time << "\n";
    std::cout << "SENDING Time (ms): " << send_time << "\n";
    std::cout << "=========================\n";
}

// ================= RECEIVER =================

void run_receiver(const std::string& filename) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 1);

    std::cout << "Receiver waiting...\n";
    int client = accept(server, nullptr, nullptr);

    size_t size;
    auto start_recv = std::chrono::high_resolution_clock::now();
    recv(client, &size, sizeof(size), 0);

    std::vector<Byte> encrypted(size);
    recv(client, encrypted.data(), encrypted.size(), 0);
    auto end_recv = std::chrono::high_resolution_clock::now();

    // DECRYPT
    auto start_dec = std::chrono::high_resolution_clock::now();
    std::vector<Byte> decrypted;
    decrypt_data(encrypted, decrypted);
    auto end_dec = std::chrono::high_resolution_clock::now();

    std::ofstream out(filename, std::ios::binary);
    out.write((char*)decrypted.data(), decrypted.size());
    out.close();

    close(client);
    close(server);

    auto recv_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_recv - start_recv).count();
    auto dec_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_dec - start_dec).count();

    std::cout << "\n===== RECEIVER METRICS =====\n";
    std::cout << "RECEIVING Time (ms): " << recv_time << "\n";
    std::cout << "Decryption Time (ms): " << dec_time << "\n";
    std::cout << "===========================\n";
}

// ================= MAIN =================

int main(int argc, char* argv[]) {
    init_crypto();

    if (argc < 3) {
        std::cout << "Usage:\n";
        std::cout << "  Receiver: ./sftp_modified receiver output.bin\n";
        std::cout << "  Sender:   ./sftp_modified sender input.bin\n";
        return 0;
    }

    std::string mode = argv[1];
    std::string file = argv[2];

    if (mode == "receiver") run_receiver(file);
    else if (mode == "sender") run_sender(file);
    else std::cout << "Invalid mode\n";

    return 0;
}
