

#include "pak.hpp"
#include "argparse.hpp"

int main(int argc, char** argv) {
	argparse::ArgumentParser parser("paktool");

	parser.add_argument("-l", "--list")
		.help("List files contained within a PAK file")
		.implicit_value(true)
		.default_value(false);
	parser.add_argument("-d", "--details")
		.help("Display additional details when listing contents of the archive")
		.implicit_value(true)
		.default_value(false);
	parser.add_argument("-c", "--create")
		.help("Create a PAK file with this name")
		.nargs(1);
	parser.add_argument("-x", "--extract")
		.help("Extract PAK file to this directory")
		.nargs(1);
	parser.add_argument("-i", "--info")
		.help("Display basic info about this PAK file")
		.default_value(false)
		.implicit_value(true);
	parser.add_argument("-h", "--help")
		.help("Display help text")
		.default_value(false)
		.implicit_value(true);
	parser.add_argument("-o", "--output")
		.help("If extracting, this is the output directory. If packing, this is the resulting pak name")
		.nargs(1);
	parser.add_argument("-v", "--verbose")
		.help("Verbose mode, print what's extracted where and stuff")
		.default_value(false)
		.implicit_value(true);
	parser.add_argument("files")
		.remaining()
		.help("PAK files to process");

	parser.parse_args(argc, argv);

	auto usage = [&](int exitcode) {
		parser.print_help();
		exit(exitcode);
	};

	if (parser.is_used("-h"))
		usage(0);

	const bool verbose = parser.get<bool>("-v");

	/* Extract PAK file */
	if (parser.is_used("-x")) {
		auto apath = parser.get("-x");
		auto odir = parser.get("-o");
		if (odir.empty()) {
			odir = apath;
			auto l = odir.find_last_of(".");
			odir.erase(odir.begin()+l);
			odir.append("/");
		}

		paklib::pak_archive archive;
		if (!archive.open(apath.c_str())) {
			fprintf(stderr, "Unable to open archive %s\n", apath.c_str());
			exit(1);
		}

		std::filesystem::create_directory(odir);

		for (auto [name, d] : archive) {
			/* Compute directory */
			auto dir = name;
			bool isdir = false;
			if (auto pos = dir.find_last_of("/"); pos != std::string::npos) {
				isdir = true;
				dir.erase(pos);
			}

			/* Ensure directory exists */
			if (isdir) {
				dir.insert(0, odir + "/");
				std::filesystem::create_directories(dir);
			}

			auto opath = odir + "/" + name;
			if (archive.extract_file(name, opath))
				if (verbose)
					printf("%s -> %s\n", name.c_str(), opath.c_str());
				else;
			else
				printf("Unable to extract %s\n", name.c_str());
		}

		archive.close();
	}
	/* Create new archive */
	else if (parser.is_used("-c")) {
		auto dir = parser.get<std::vector<std::string>>("files")[0];
		auto out = parser.get("-c");

		paklib::pak_builder builder;
		
		int filecount = 0;
		for (auto f : std::filesystem::recursive_directory_iterator(dir)) {
			if (f.is_directory())
				continue;

			/* Compute relative path inside of the PAK based on the top-level directory */
			auto pp = f.path().string();
			if (auto pos = pp.find_first_of(dir); pos != std::string::npos)
				pp.erase(pos, dir.size() + 1);
			if (verbose)
				printf("Added %s as %s\n", f.path().string().c_str(), pp.c_str());
			builder.add_file(f, pp);
			++filecount;
		}

		if (!builder.write(out)) {
			fprintf(stderr, "Failed to save archive '%s'\n", out.c_str());
			exit(1);
		}

		printf("Wrote archive '%s' with %d files\n", out.c_str(), filecount);
	}
	/* Detail querying */
	else {
		auto archives = parser.get<std::vector<std::string>>("files");
		for (auto& arch : archives) {
			paklib::pak_archive archive;
			if (!archive.open(arch.c_str())) {
				fprintf(stderr, "Unable to open archive %s\n", arch.c_str());
				exit(1);
			}

			if (parser.is_used("-i")) {
				printf("ID PAK archive, %d files\n", archive.file_count());
			}

			const bool details = parser.is_used("-d");
			if (parser.is_used("-l")) {
				for (auto [name, det] : archive) {
					printf("%s\n", name.c_str());
					if (details) {
						printf("  size:   %d (%d KiB)\n", det.size, det.size / 1024);
						printf("  offset: 0x%X\n", det.offset);
					}
				}
			}
		}
	}
}
