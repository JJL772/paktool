
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <string>

namespace paklib
{
#pragma pack(1)
	struct pak_header_t {
		char id[4];
		uint32_t offset;
		uint32_t size;
	};

	constexpr int MAX_PAK_NAME_LEN = 56;

	struct pak_file_t {
		char name[MAX_PAK_NAME_LEN];
		uint32_t offset;
		uint32_t size;
	};
#pragma pack()

	struct pak_file_details_t {
		uint32_t offset;
		uint32_t size;
	};

	enum PakError {
		NoError,
		OpenFailed,
		InvalidHeader,
		InvalidFileEntry,
	};

	/**
	 * \brief Read-only view of a PAK file
	 */
	class pak_archive {
	public:
		pak_archive() = default;
		pak_archive(const pak_archive&) = delete;
		pak_archive(pak_archive&&) = delete;

		~pak_archive() {
			if (m_file)
				fclose(m_file);
		}

		inline bool good() const { return m_file != nullptr && m_errno == PakError::NoError; }
		inline PakError last_error() const { return m_errno; }
		inline int file_count() const { return m_files.size(); }

		/**
		 * \brief Open a PAK file off of disk from the specified path
		 * Reads the header and file entries
		 */
		bool open(const char* path) {
			close();

			m_errno = NoError;
			m_file = fopen(path, "rb");
			if (!m_file) {
				m_errno = OpenFailed;
				return false;
			}
			
			fseek(m_file, 0, SEEK_END);
			m_fileSize = ftell(m_file);
			fseek(m_file, 0, SEEK_SET);

			if (m_fileSize < sizeof(pak_header_t)) {
				m_errno = InvalidHeader;
				fclose(m_file);
				return false;
			}

			/* Read header */
			pak_header_t hdr;
			if (fread(&hdr, sizeof(hdr), 1, m_file) != 1 || 
				!(hdr.id[0] == 'P' && hdr.id[1] == 'A' && hdr.id[2] == 'C' && hdr.id[3] == 'K')) {
				m_errno = InvalidHeader;
				fclose(m_file);
				return false;
			}

			fseek(m_file, hdr.offset, SEEK_SET);

			/* Reserve space for this many files */
			m_files.resize(hdr.size / sizeof(pak_file_t));

			/* Read big block of files */
			if (fread(&m_files.at(0), hdr.size, 1, m_file) != 1) {
				m_errno = InvalidFileEntry;
				fclose(m_file);
				return false;
			}

			/* Populate fast lookup list */
			for (int i = 0; i < m_files.size(); ++i) {
				char name[MAX_PAK_NAME_LEN+1] {};
				std::memcpy(name, m_files[i].name, MAX_PAK_NAME_LEN);
				m_lookupMap.insert({name, i});
			}
			return true;
		}

		void close() {
			if (m_file)
				fclose(m_file);
			m_file = nullptr;
			m_files.clear();
			m_lookupMap.clear();
		}

		bool read_file(const std::string& pak_path, void* outbuf, size_t size) {
			if (auto it = m_lookupMap.find(pak_path); it != m_lookupMap.end()) {
				size_t fz = m_files[it->second].size;
				size_t toread = fz < size ? fz : size;
				fseek(m_file, m_files[it->second].offset, SEEK_SET);
				return fread(outbuf, size, 1, m_file) == 1;
			}
			return false;
		}

		/**
		 * \brief Extract file from the PAK to disk
		 * \param pak_path Path of the file within the pak file
		 * \param out Path on disk
		 */
		bool extract_file(const std::string& pak_path, const std::string& out) {
			if (auto it = m_lookupMap.find(pak_path); it != m_lookupMap.end())
			{
				auto* fp = fopen(out.c_str(), "wb");
				if (!fp)
					return false;

				auto& f = m_files[it->second];
				fseek(m_file, f.offset, SEEK_SET);
				int64_t sz = f.size;

				char buf[8192];
				for(;sz > 0; sz -= sizeof(buf)) {
					fread(buf, sizeof(buf) > sz ? sz : sizeof(buf), 1, m_file);
					fwrite(buf, sizeof(buf) > sz ? sz : sizeof(buf), 1, fp);
				}

				fclose(fp);
				return true;
			}
			return false;
		}

		bool stat(const std::string& pak_path, size_t& file_size, size_t& offset) {
			if (auto it = m_lookupMap.find(pak_path); it != m_lookupMap.end()) {
				file_size = m_files[it->second].size;
				offset = m_files[it->second].offset;
				return true;
			}
			return false;
		}

		class iterator {
			int m_file;
			pak_archive* m_archive;
		public:
			using iterator_category = std::forward_iterator_tag;
			using difference_type = decltype(m_file);
			using value_type = std::string;
			using pointer = std::string*;
			using reference = std::string&;

			iterator(int start, pak_archive* a) : m_file(start), m_archive(a) {};

			std::pair<std::string, pak_file_details_t> operator*() const {
				char p[MAX_PAK_NAME_LEN+1]{};
				auto f = m_archive->m_files[m_file];
				std::memcpy(p, f.name, MAX_PAK_NAME_LEN);
				return std::make_pair<std::string, pak_file_details_t>(p, { f.offset,f.size });
			}

			inline iterator& operator++() { m_file++; return *this; }
			inline iterator& operator++(int) { m_file++; return *this; }

			friend bool operator==(const iterator& a, const iterator& b) { return a.m_file == b.m_file; }
			friend bool operator!=(const iterator& a, const iterator& b) { return a.m_file != b.m_file; }
		};

		iterator begin() { return iterator(0, this); }
		iterator end() { return iterator(m_files.size(), this); }


	protected:
		FILE* m_file = nullptr;
		size_t m_fileSize = 0;
		std::vector<pak_file_t> m_files;
		PakError m_errno = NoError;
		std::unordered_map<std::string, int> m_lookupMap;
	};

	/**
	 * \brief Simple PAK file builder
	 * Use this to build a new pak file
	 */
	class pak_builder {
	public:
	
		bool add_file(const std::filesystem::path& disk_path, const std::string& pak_path) {
			if (pak_path.size() > MAX_PAK_NAME_LEN)
				return false;
			size_t sz;
			if ((sz = std::filesystem::file_size(disk_path)) > UINT32_MAX)
				return false;
			m_files.push_back({});
			auto& f = m_files.back();
			f.size = sz;
			f.disk_path = disk_path;
			std::strncpy(f.pak_path, pak_path.c_str(), MAX_PAK_NAME_LEN);
			return true;
		}

		bool write(const std::string& file) {
			std::ofstream stream(file, std::ios::binary | std::ios::out);
			if (!stream.good())
				return false;
			
			/* Build global PAK header */
			pak_header_t hdr;
			strncpy(hdr.id, "PACK", sizeof(hdr.id));
			hdr.offset = sizeof(pak_header_t);
			hdr.size = m_files.size() * sizeof(pak_file_t);
			stream.write(reinterpret_cast<char*>(&hdr), sizeof(hdr));

			/* Build file listing */
			/* This could be done faster, but don't want to blow up the stack if we have many files */
			size_t curoff = sizeof(hdr) + hdr.size;
			for (auto& file : m_files) {
				pak_file_t ent;
				std::strncpy(ent.name, file.pak_path, MAX_PAK_NAME_LEN);
				ent.size = file.size;
				ent.offset = curoff;

				file.offset = curoff;

				curoff += file.size;

				stream.write(reinterpret_cast<char*>(&ent), sizeof(ent));
			}

			/* Pass 2: Write file data */
			for (auto& file : m_files) {
				std::ifstream istream(file.disk_path, std::ios::binary | std::ios::in);
				if (!istream.good())
					return false; /* Urgh.. */

				stream.seekp(file.offset);

				int64_t sz = file.size;
				char buf[8192];
				for (;sz > 0; sz -= sizeof(buf)) {
					istream.read(buf, sizeof(buf));
					stream.write(buf, sizeof(buf) > sz ? sz : sizeof(buf));
				}

				istream.close();
			}

			stream.close();
			return true;
		}

	protected:
		struct file_t {
			std::filesystem::path disk_path;
			char pak_path[MAX_PAK_NAME_LEN+1] {};
			size_t offset;
			size_t size;
		};

		std::vector<file_t> m_files;
	};
}