/*
 See BinaryAddressMap.h
*/

#include "BinaryAddressMap.h"

#include "Utils.h"

#include <cstdlib>
#include <cstdio>
#include <climits>
#include <map>
#include <string>
#include <sstream>
#include <vector>

namespace clang_mutate{
  // Executes a command a returns the output as a vector of strings
  static std::vector<std::string> exec(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    std::vector<std::string> lines;
    char buffer[256];

    if (pipe) {
      while (!feof(pipe)) {
        if (fgets(buffer, 256, pipe) != NULL)
          lines.push_back(buffer);
      }
    }
    pclose(pipe);

    return lines;
  }

  // Splits a string into a vector of tokens using 'delim' as
  // a delimiter.
  std::vector<std::string> split(
    const std::string &s,
    const std::string &delim)
  {
    size_t i = s.find_first_not_of(delim, 0);
    size_t j = i;
    std::vector<std::string> tokens;

    while ( (j = s.find_first_of(delim, i)) != std::string::npos )
    {
      tokens.push_back( s.substr(i, j-i) );
      i = s.find_first_not_of(delim, j+1);
    }

    if ( i != std::string::npos )
      tokens.push_back(s.substr(i));

    return tokens;
  }

  BinaryAddressMap::FilenameLineNumAddressPair
  BinaryAddressMap::parseAddressLine(
    const std::string &line,
    const std::string &nextLine,
    const std::vector<std::string> &files ) {

    // Regular expressions would be cool...
    std::stringstream lineEntriesStream;
    FilenameLineNumAddressPair filenameLineNumAddressPair;
    unsigned long beginAddress;
    unsigned long endAddress;
    unsigned int lineNum;
    unsigned int tmp, fileIndex;

    // Special: If end_sequence is present, this is the end address of the current
    // line number. We will just store it as the next line number.
    bool endSequence = line.find("end_sequence") != std::string::npos;

    lineEntriesStream.str( line );
    lineEntriesStream >> std::hex >> beginAddress >> std::dec >> lineNum >> tmp >> fileIndex;
    lineEntriesStream.str( nextLine );
    lineEntriesStream >> std::hex >> endAddress;

    filenameLineNumAddressPair.first = files[fileIndex-1];
    filenameLineNumAddressPair.second.first = (endSequence) ? lineNum+1 : lineNum;
    filenameLineNumAddressPair.second.second.first  = beginAddress;
    filenameLineNumAddressPair.second.second.second = endAddress;

    return filenameLineNumAddressPair;
  }

  std::string BinaryAddressMap::parseDirectoryLine(
    const std::string &line,
    const std::set<std::string>& sourcePaths ) {
    // Regular expressions would be cool...
    size_t startquote = line.find_first_of('\'') + 1;
    size_t endquote = line.find_last_of('\'') - 1;
    std::string directory = line.substr( startquote, endquote - startquote + 1);

    return findOnSourcePath(sourcePaths, directory.c_str());
  }

  std::string BinaryAddressMap::findOnSourcePath(
    const std::set<std::string>& sourcePaths,
    const std::string& directory,
    const std::string& fileName)
  {
    std::string path = Utils::rtrim(directory, "/") + "/" + fileName;
    std::string rpath = Utils::safe_realpath(path);
    if (!rpath.empty()) {
      return rpath;
    }
    else if (m_dwarfFilepathMap.find(path) != m_dwarfFilepathMap.end()) {
      return m_dwarfFilepathMap[path];
    }
    else {
      for ( std::set<std::string>::const_iterator sourcePathIter = sourcePaths.begin();
            sourcePathIter != sourcePaths.end();
            sourcePathIter++ )
      {
        path = Utils::rtrim(*sourcePathIter, "/") + "/" +
               Utils::rtrim(directory, "/") + "/" +
               fileName;
        rpath = Utils::safe_realpath(path);
        if (!rpath.empty()) {
          return rpath;
        }
        else if (m_dwarfFilepathMap.find(path) != m_dwarfFilepathMap.end()) {
          return m_dwarfFilepathMap[path];
        }
      }
    }

    return "";
  }

  std::string BinaryAddressMap::parseFileLine(
    const std::string &line,
    const std::set<std::string>& sourcePaths,
    const std::vector<std::string> &directories ) {

    // Regular expresssions would be cool...
    std::stringstream lineEntriesStream( line.substr( line.find_last_of(']')+1 ) );
    unsigned int directoryIndex;
    std::string tmp1, tmp2, fileName;

    lineEntriesStream >> directoryIndex >> tmp1 >> tmp2 >> fileName;

    return (directoryIndex > 0) ?
            findOnSourcePath( sourcePaths, directories[directoryIndex-1], fileName ) :
            findOnSourcePath( sourcePaths, ".", fileName);
  }

  BinaryAddressMap::FilesMap
  BinaryAddressMap::parseCompilationUnit(
    const std::vector<std::string>& dwarfDumpDebugLine,
    const std::set<std::string>& sourcePaths,
    unsigned long long &currentline ) {

    FilesMap filesMap;
    std::vector<std::string> directories;
    std::vector<std::string> files;

    currentline++;

    while ( currentline < dwarfDumpDebugLine.size() &&
            dwarfDumpDebugLine[currentline].find("Line table prologue:") == std::string::npos ) {
      std::string line = dwarfDumpDebugLine[currentline];
      std::string directory;
      std::string file;

      if ( line.find("include_directories") == 0 ) {
        directory = parseDirectoryLine( line, sourcePaths );
        directories.push_back( directory );
      }

      if ( line.find("file_names") == 0 ) {
        file = parseFileLine( line, sourcePaths, directories );
        files.push_back( file );
      }

      if ( line.find("0x") == 0 &&
           currentline+1 < dwarfDumpDebugLine.size() &&
           dwarfDumpDebugLine[currentline+1].find("0x") == 0 ) {
        std::string nextLine = dwarfDumpDebugLine[currentline+1];
        FilenameLineNumAddressPair fileNameLineNumAddressPair =
            parseAddressLine( line, nextLine, files );

        LineNumsToAddressesMap & ln2am = filesMap[fileNameLineNumAddressPair.first];
        if (!ln2am.insert( fileNameLineNumAddressPair.second ).second)
        {
            // Already had an address range for this line; we need to expand it.
            AddressRange & range = ln2am[fileNameLineNumAddressPair.second.first];
            if (range.first > fileNameLineNumAddressPair.second.second.first)
                range.first = fileNameLineNumAddressPair.second.second.first;
            if (range.second < fileNameLineNumAddressPair.second.second.second)
                range.second = fileNameLineNumAddressPair.second.second.second;
        }
      }

      currentline++;
    }

    currentline--;

    return filesMap;
  }

  std::set< std::string > BinaryAddressMap::getSourcePaths(
    const std::vector<std::string>& dwarfDumpDebugInfo)
  {
    std::set< std::string > sourcePaths;

    sourcePaths.insert( "." );
    for ( std::vector<std::string>::const_iterator lineIter = dwarfDumpDebugInfo.begin();
          lineIter != dwarfDumpDebugInfo.end();
          lineIter++ )
    {
      if ( lineIter->find("DW_AT_comp_dir") != std::string::npos )
      {
        size_t pathStart = lineIter->find_first_of("\"") + 1;
        size_t pathEnd = lineIter->find_last_of("\"") - 1;
        const std::string path =
          lineIter->substr( pathStart, pathEnd - pathStart + 1 );

        sourcePaths.insert( path );
      }
    }

    return sourcePaths;
  }

  void BinaryAddressMap::parseDwarfFilepathMapping(const std::string &dwarfFilepathMapping)
  {
    const std::vector<std::string> mappings = Utils::split(dwarfFilepathMapping, ',');

    for ( std::vector<std::string>::const_iterator iter = mappings.begin();
          iter != mappings.end();
          iter++ )
    {
      const std::vector<std::string> mapping = Utils::split(*iter, '=');
      if (mapping.size() == 2) {
        std::string dwarfFilepath = Utils::trim(mapping[0]);
        std::string newFilepath = Utils::trim(mapping[1]);

        m_dwarfFilepathMap[dwarfFilepath] = newFilepath;
      }
    }
  }

  void BinaryAddressMap::copy(const BinaryAddressMap& other){
    m_binaryPath = other.m_binaryPath;
    m_compilationUnitMap = other.m_compilationUnitMap;
    m_dwarfFilepathMap = other.m_dwarfFilepathMap;

    m_elf.load(m_binaryPath);
  }

  void BinaryAddressMap::init(const std::vector<std::string>& dwarfDumpDebugLine,
                              const std::set<std::string>& sourcePaths){
    unsigned int compilationUnit = -1;

    for ( unsigned long long currentline = 0;
          currentline < dwarfDumpDebugLine.size();
          currentline++ ) {
      if ( dwarfDumpDebugLine[currentline].find("Line table prologue:") != std::string::npos ) {
        compilationUnit++;

        m_compilationUnitMap[compilationUnit] =
          parseCompilationUnit( dwarfDumpDebugLine, sourcePaths, currentline );
      }
    }
  }

  BinaryAddressMap::BinaryAddressMap() {
  }

  // Initialize a BinaryAddressMap from an ELF executable.
  BinaryAddressMap::BinaryAddressMap(const std::string &binary,
                                     const std::string &dwarfFilepathMapping)
  {
    m_binaryPath = Utils::safe_realpath(binary);
    m_elf.load(m_binaryPath);

    parseDwarfFilepathMapping(dwarfFilepathMapping);

    if ( !m_binaryPath.empty() && Utils::fileExists(m_binaryPath) ) {
      const std::string dwarfDumpDebugLineCmd =
        LLVM_DWARFDUMP" -debug-dump=line " + m_binaryPath;
      const std::string dwarfDumpDebugInfoCmd =
        LLVM_DWARFDUMP" -debug-dump=info " + m_binaryPath;

      std::vector<std::string> dwarfDumpDebugLine =
        exec( dwarfDumpDebugLineCmd.c_str() );
      std::vector<std::string> dwarfDumpDebugInfo =
        exec( dwarfDumpDebugInfoCmd.c_str() );

      init( dwarfDumpDebugLine, getSourcePaths( dwarfDumpDebugInfo ) );
    }
  }

  BinaryAddressMap::BinaryAddressMap(const BinaryAddressMap& other) {
    copy(other);
  }

  BinaryAddressMap& BinaryAddressMap::operator=(const BinaryAddressMap& other) {
    if (this != &other) {
      copy(other);
    }

    return *this;
  }

  BinaryAddressMap::~BinaryAddressMap() {
  }

  bool BinaryAddressMap::isEmpty() const {
    return m_compilationUnitMap.empty();
  }

  std::string BinaryAddressMap::getPath() const {
    return m_binaryPath;
  }

  Utils::Optional<BinaryData>
  BinaryAddressMap::getCompilationData(const std::string & filePath,
                                       const LineRange & lineRange) const {
    BinaryData binaryData;
    Utils::Optional<AddressRange> addrRange =
        getAddressRangeForLines(filePath, lineRange);

    if (addrRange) {
      binaryData.first = addrRange.value();
      binaryData.second = getBinaryContents(addrRange.value().first,
                                            addrRange.value().second);
      return Utils::Optional<BinaryData>(binaryData);
    }
    else {
      return Utils::Optional<BinaryData>();
    }
  }

  Utils::Optional<AddressRange>
  BinaryAddressMap::getAddressRangeForLines(
    const std::string& filePath,
    const LineRange& lineRange) const {
    AddressRange addrRange;
    LineRange tempLineRange(lineRange);
    bool found = false;

    // Iterate over each compilation unit
    for ( CompilationUnitMap::const_iterator
              compilationUnitMapIter = m_compilationUnitMap.begin();
          compilationUnitMapIter != m_compilationUnitMap.end();
          compilationUnitMapIter++ )
    {
        FilesMap const& filesMap = compilationUnitMapIter->second;

        // Check if file is in the files for this compilation unit
        FilesMap::const_iterator fmsearch = filesMap.find( filePath );
        if ( fmsearch == filesMap.end() )
            continue;

        LineNumsToAddressesMap const& ln2am = fmsearch->second;
        LineNumsToAddressesMap::const_iterator search = ln2am.end();

        // Find the first line with address information
        while (search == ln2am.end() &&
               tempLineRange.first <= lineRange.second) {
            search = ln2am.find(tempLineRange.first);
            tempLineRange.first++;
        }

        while (search != ln2am.end() && search->first <= lineRange.second) {
            if (!found) {
                // First address found in the given line range.
                addrRange = search->second;
                found = true;
            }
            else {
                // Found another address range in the line range,
                // expand the current address range.
                if (search->second.first < addrRange.first)
                    addrRange.first = search->second.first;
                if (search->second.second > addrRange.second)
                    addrRange.second = search->second.second;
            }
            ++search;
        }
    }

    return found?
           Utils::Optional<AddressRange>(addrRange) :
           Utils::Optional<AddressRange>();
  }

  Bytes BinaryAddressMap::getBinaryContents(
    unsigned long beginAddress,
    unsigned long endAddress ) const
  {
    Bytes bytes;

    // Note: When compiled with optimization turned on, there is no longer
    // a one-to-one correspondence of line of C/C++ source code ->
    // address range in the binary due to inlining of function calls
    // and other techniques. Additionally, the ordering of function calls in
    // the compiled binary may not match the ordering in source.
    if ( beginAddress < endAddress )
    {
      for (unsigned int i = 0; i < m_elf.sections.size(); i++) {
        ELFIO::section* sec = m_elf.sections[i];
        if ( sec->get_address() <= beginAddress &&
             endAddress < sec->get_address() + sec->get_size() ) {
          const char* data = sec->get_data();

          bytes.assign( data + beginAddress - sec->get_address(),
                        data + endAddress - sec->get_address() );
        }
      }
    }

    return bytes;
  }
}
