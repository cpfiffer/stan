#ifndef STAN_IO_STAN_CSV_READER_HPP
#define STAN_IO_STAN_CSV_READER_HPP

#include <boost/algorithm/string.hpp>
#include <stan/math/prim.hpp>
#include <cctype>
#include <istream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace stan {
namespace io {

inline void prettify_stan_csv_name(std::string& variable) {
  if (variable.find_first_of(":.") != std::string::npos) {
    std::vector<std::string> parts;
    boost::split(parts, variable, boost::is_any_of(":"));
    for (auto& part : parts) {
      int pos = part.find('.');
      if (pos > 0) {
        part[pos] = '[';
        std::replace(part.begin(), part.end(), '.', ',');
        part += "]";
      }
    }
    variable = boost::algorithm::join(parts, ".");
  }
}

struct stan_csv_metadata {
  int stan_version_major;
  int stan_version_minor;
  int stan_version_patch;

  std::string model;
  std::string data;
  std::string init;
  size_t chain_id;
  size_t seed;
  bool random_seed;
  size_t num_samples;
  size_t num_warmup;
  bool save_warmup;
  size_t thin;
  bool append_samples;
  std::string algorithm;
  std::string engine;
  int max_depth;

  stan_csv_metadata()
      : stan_version_major(0),
        stan_version_minor(0),
        stan_version_patch(0),
        model(""),
        data(""),
        init(""),
        chain_id(1),
        seed(0),
        random_seed(false),
        num_samples(0),
        num_warmup(0),
        save_warmup(false),
        thin(0),
        append_samples(false),
        algorithm(""),
        engine(""),
        max_depth(10) {}
};

struct stan_csv_adaptation {
  double step_size;
  Eigen::MatrixXd metric;

  stan_csv_adaptation() : step_size(0), metric(0, 0) {}
};

struct stan_csv_timing {
  double warmup;
  double sampling;

  stan_csv_timing() : warmup(0), sampling(0) {}
};

struct stan_csv {
  stan_csv_metadata metadata;
  std::vector<std::string> header;
  stan_csv_adaptation adaptation;
  Eigen::MatrixXd samples;
  stan_csv_timing timing;
};

/**
 * Reads from a Stan output csv file.
 */
class stan_csv_reader {
 public:
  stan_csv_reader() {}
  ~stan_csv_reader() {}

  static bool read_metadata(std::istream& in, stan_csv_metadata& metadata,
                            std::ostream* out) {
    std::stringstream ss;
    std::string line;

    if (in.peek() != '#')
      return false;
    while (in.peek() == '#') {
      std::getline(in, line);
      ss << line << '\n';
    }
    ss.seekg(std::ios_base::beg);

    char comment;
    std::string lhs;

    std::string name;
    std::string value;

    while (ss.good()) {
      ss >> comment;
      std::getline(ss, lhs);

      size_t equal = lhs.find("=");
      if (equal != std::string::npos) {
        name = lhs.substr(0, equal);
        boost::trim(name);
        value = lhs.substr(equal + 1, lhs.size());
        boost::trim(value);
        boost::replace_first(value, " (Default)", "");
      } else {
        if (lhs.compare(" data") == 0) {
          ss >> comment;
          std::getline(ss, lhs);

          size_t equal = lhs.find("=");
          if (equal != std::string::npos) {
            name = lhs.substr(0, equal);
            boost::trim(name);
            value = lhs.substr(equal + 2, lhs.size());
            boost::replace_first(value, " (Default)", "");
          }

          if (name.compare("file") == 0)
            metadata.data = value;

          continue;
        }
      }

      if (name.compare("stan_version_major") == 0) {
        std::stringstream(value) >> metadata.stan_version_major;
      } else if (name.compare("stan_version_minor") == 0) {
        std::stringstream(value) >> metadata.stan_version_minor;
      } else if (name.compare("stan_version_patch") == 0) {
        std::stringstream(value) >> metadata.stan_version_patch;
      } else if (name.compare("model") == 0) {
        metadata.model = value;
      } else if (name.compare("num_samples") == 0) {
        std::stringstream(value) >> metadata.num_samples;
      } else if (name.compare("num_warmup") == 0) {
        std::stringstream(value) >> metadata.num_warmup;
      } else if (name.compare("save_warmup") == 0) {
        // cmdstan args can be "true" and "false", was "1", "0"
        if (value.compare("true") == 0) {
          value = "1";
        }
        std::stringstream(value) >> metadata.save_warmup;
      } else if (name.compare("thin") == 0) {
        std::stringstream(value) >> metadata.thin;
      } else if (name.compare("id") == 0) {
        std::stringstream(value) >> metadata.chain_id;
      } else if (name.compare("init") == 0) {
        metadata.init = value;
        boost::trim(metadata.init);
      } else if (name.compare("seed") == 0) {
        std::stringstream(value) >> metadata.seed;
        metadata.random_seed = false;
      } else if (name.compare("append_samples") == 0) {
        std::stringstream(value) >> metadata.append_samples;
      } else if (name.compare("algorithm") == 0) {
        metadata.algorithm = value;
      } else if (name.compare("engine") == 0) {
        metadata.engine = value;
      } else if (name.compare("max_depth") == 0) {
        std::stringstream(value) >> metadata.max_depth;
      }
    }
    if (ss.good() == true)
      return false;

    return true;
  }  // read_metadata

  static bool read_header(std::istream& in, std::vector<std::string>& header,
                          std::ostream* out, bool prettify_name = true) {
    std::string line;

    if (!std::isalpha(in.peek()))
      return false;

    std::getline(in, line);
    std::stringstream ss(line);

    header.resize(std::count(line.begin(), line.end(), ',') + 1);
    int idx = 0;
    while (ss.good()) {
      std::string token;
      std::getline(ss, token, ',');
      boost::trim(token);

      if (prettify_name) {
        prettify_stan_csv_name(token);
      }
      header[idx++] = token;
    }
    return true;
  }

  static bool read_adaptation(std::istream& in, stan_csv_adaptation& adaptation,
                              std::ostream* out) {
    std::stringstream ss;
    std::string line;
    int lines = 0;

    if (in.peek() != '#' || in.good() == false)
      return false;

    while (in.peek() == '#') {
      std::getline(in, line);
      ss << line << std::endl;
      lines++;
    }
    ss.seekg(std::ios_base::beg);

    if (lines < 4)
      return false;

    char comment;  // Buffer for comment indicator, #

    // Skip first two lines
    std::getline(ss, line);

    // Stepsize
    std::getline(ss, line, '=');
    boost::trim(line);
    ss >> adaptation.step_size;

    // Metric parameters
    std::getline(ss, line);
    std::getline(ss, line);
    std::getline(ss, line);

    int rows = lines - 3;
    int cols = std::count(line.begin(), line.end(), ',') + 1;
    adaptation.metric.resize(rows, cols);

    for (int row = 0; row < rows; row++) {
      std::stringstream line_ss;
      line_ss.str(line);
      line_ss >> comment;

      for (int col = 0; col < cols; col++) {
        std::string token;
        std::getline(line_ss, token, ',');
        boost::trim(token);
        std::stringstream(token) >> adaptation.metric(row, col);
      }
      std::getline(ss, line);  // Read in next line
    }

    if (ss.good())
      return false;
    else
      return true;
  }

  static bool read_samples(std::istream& in, Eigen::MatrixXd& samples,
                           stan_csv_timing& timing, std::ostream* out) {
    std::stringstream ss;
    std::string line;

    int rows = 0;
    int cols = -1;

    if (in.peek() == '#' || in.good() == false)
      return false;

    while (in.good()) {
      bool comment_line = (in.peek() == '#');
      bool empty_line = (in.peek() == '\n');

      std::getline(in, line);
      if (empty_line)
        continue;
      if (!line.length())
        break;

      if (comment_line) {
        if (line.find("(Warm-up)") != std::string::npos) {
          int left = 17;
          int right = line.find(" seconds");
          double warmup;
          std::stringstream(line.substr(left, right - left)) >> warmup;
          timing.warmup += warmup;
        } else if (line.find("(Sampling)") != std::string::npos) {
          int left = 17;
          int right = line.find(" seconds");
          double sampling;
          std::stringstream(line.substr(left, right - left)) >> sampling;
          timing.sampling += sampling;
        }
      } else {
        ss << line << '\n';
        int current_cols = std::count(line.begin(), line.end(), ',') + 1;
        if (cols == -1) {
          cols = current_cols;
        } else if (cols != current_cols) {
          if (out)
            *out << "Error: expected " << cols << " columns, but found "
                 << current_cols << " instead for row " << rows + 1
                 << std::endl;
          return false;
        }
        rows++;
      }

      in.peek();
    }

    ss.seekg(std::ios_base::beg);

    if (rows > 0) {
      samples.resize(rows, cols);
      for (int row = 0; row < rows; row++) {
        std::getline(ss, line);
        std::stringstream ls(line);
        for (int col = 0; col < cols; col++) {
          std::getline(ls, line, ',');
          boost::trim(line);
          std::stringstream(line) >> samples(row, col);
        }
      }
    }
    return true;
  }

  /**
   * Parses the file.
   *
   * Warns if missing metatdata, inconsistencies between metadata config
   * and parsed data rows.
   *
   * Throws exception if no header row found.
   *
   * @param[in] in input stream to parse
   * @param[out] out output stream to send messages
   */
  static stan_csv parse(std::istream& in, std::ostream* out) {
    stan_csv data;

    if (!read_metadata(in, data.metadata, out)) {
      if (out)
        *out << "Warning: non-fatal error reading metadata" << std::endl;
    }

    if (!read_header(in, data.header, out)) {
      if (out)
        *out << "Error: error reading header" << std::endl;
      throw std::invalid_argument("Error with header of input file in parse");
    }

    // skip warmup draws, if any
    if (data.metadata.algorithm != "fixed_param" && data.metadata.num_warmup > 0
        && data.metadata.save_warmup) {
      std::string line;
      while (in.peek() != '#') {
        std::getline(in, line);
      }
    }

    if (data.metadata.algorithm != "fixed_param"
        && !read_adaptation(in, data.adaptation, out)) {
      if (out)
        *out << "Warning: non-fatal error reading adaptation data" << std::endl;
    }

    data.timing.warmup = 0;
    data.timing.sampling = 0;

    if (!read_samples(in, data.samples, data.timing, out)) {
      if (out)
        *out << "Warning: non-fatal error reading samples" << std::endl;
    }

    if (data.metadata.thin > 0) {
      int expected_samples = data.metadata.num_samples / data.metadata.thin;
      if (expected_samples != data.samples.rows()) {
        std::stringstream msg;
        msg << ", expecting " << expected_samples << " samples, found "
            << data.samples.rows();
        if (out)
          *out << "Warning: error reading samples" << msg.str() << std::endl;
      }
    }
    return data;
  }
};

}  // namespace io

}  // namespace stan

#endif
