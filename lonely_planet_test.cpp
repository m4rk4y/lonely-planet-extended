// Compile, link and run. Simples.
// Has no external library dependencies apart from STL.
//
// Uses RapidXml for XML parsing. This was a fairly arbitrary choice.
//
// Run as:
//
// lonely_planet_test <taxonomy-xml-file> <destinations-xml-file> <output-directory> [ <section-names> ]
// where <section-names> defaults to "overview".
//
// Does *not* create <output-directory>.
//
// TODO: read template from external file.
// TODO: identify different content sections e.g.
// Overview
// <overview-stuff>
// Money
// <money-stuff>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <sstream>

#include <rapidxml.hpp>
#include <rapidxml_print.hpp>

using namespace std;
using namespace rapidxml;

//============================================================================
// Class declarations. Normally I would put these in a header for external
// use, but this is stand-alone.

class XmlReader
{
    public:
        XmlReader ( const char * fileSignifier,
                    const char * fileName
                  );
        virtual void readAndParse();
        const xml_document<char> & getDocument() const;

    protected:
        xml_document<char> m_document;

    private:
        ifstream m_file;
        string m_contents;
};

class TaxonomyReader : public XmlReader
{
    public:
        TaxonomyReader ( const char * fileName ) :
            XmlReader ( "taxonomy", fileName ) {}
};

class DestinationsReader : public XmlReader
{
    public:
        DestinationsReader ( const char * fileName ) :
            XmlReader ( "destinations", fileName ) {}
        void generateDestinationDescriptions
        (   const set<string> & sectionNames
        );
        string getDestinationDescription ( int node_id ) const;

    private:
        void getSubTreeContent ( xml_node< char > * node );

        const set<string> * m_sectionNames;
        map<int, string> m_descriptions;
        string m_combinedContent;
};

// I could put the template parts in an external file(s) and read them
// (just once of course).
// I could reduce the number of parts by having substitution points.
class HtmlTemplate
{
    public:
        static HtmlTemplate * createHtmlTemplate(); // enforce singleton
        const string & getPart1() const { return m_part1; }
        const string & getPart2() const { return m_part2; }
        const string & getPart3() const { return m_part3; }
        const string & getPart4() const { return m_part4; }
        const string & getPart5() const { return m_part5; }

    private:
        HtmlTemplate(); // enforce singleton
        string m_part1;
        string m_part2;
        string m_part3;
        string m_part4;
        string m_part5;
};

class HtmlGenerator
{
    public:
        HtmlGenerator
        (   const TaxonomyReader & taxonomyReader,
            const DestinationsReader & destinationsReader
        ) : m_taxonomyReader ( taxonomyReader ),
            m_destinationsReader ( destinationsReader ),
            m_outputDirectory ( "" ),
            m_template ( HtmlTemplate::createHtmlTemplate() )
        {}
        void generateFiles ( const char * outputDirName );

    private:
        void generateFilesForTree
        (   xml_node<char> * parent,
            xml_node<char> * node
        ) const;
        void generateFile
        (   xml_node<char> * parent,
            xml_node<char> * node
        ) const;
        string makeHtmlFileName ( xml_attribute< char > * node_id ) const;

        const TaxonomyReader & m_taxonomyReader;
        const DestinationsReader & m_destinationsReader;
        string m_outputDirectory;
        HtmlTemplate * m_template;
};

//============================================================================

extern int main ( int argc, char ** argv )
{
    // Check arguments.
    if ( argc < 4 )
    {
        cerr << "Error: (" << argv[0] << ") needs <taxonomy-xml-file> <destinations-xml-file> <output-directory> [ <section-names> ]" << endl;
        return 1;
    }

    // Get optional section names. If none supplied, use "overview".
    set<string> sectionNames;
    for ( int inx = 4; inx < argc; ++inx )
    {
        sectionNames.insert ( argv[inx] );
    }
    if ( sectionNames.empty() )
    {
        sectionNames.insert ( "overview" );
    }

    try
    {
        // Slurp and parse entire files. Note that because of the way that
        // RapidXml works, we need to hang on to the file content strings as well
        // as the generated XML tree (because the tree points directly back into
        // the parsed text rather than making its own string copies).
        TaxonomyReader taxonomyReader ( argv[1] );
        taxonomyReader.readAndParse();

        DestinationsReader destinationsReader ( argv[2] );
        destinationsReader.readAndParse();
        destinationsReader.generateDestinationDescriptions ( sectionNames );

        HtmlGenerator htmlGenerator ( taxonomyReader, destinationsReader );
        htmlGenerator.generateFiles ( argv[3] );
    }
    catch ( const string & error )
    {
        cerr << "Caught exception: " << error << endl;
        return 1;
    }
    catch ( ... )
    {
        cerr << "Caught unknown exception" << endl;
        return 1;
    }

    return 0;
}

//============================================================================

XmlReader::XmlReader
(   const char * fileSignifier,
    const char * fileName
)
{
    m_file.open ( fileName, ios::in );
    if ( ! m_file.is_open() )
    {
        stringstream errorStream;
        errorStream << "Failed to open " << fileSignifier << " file " << fileName << " for reading";
        throw errorStream.str();
    }
}

//----------------------------------------------------------------------------

void XmlReader::readAndParse()
{
    string fileLine;
    while ( getline ( m_file, fileLine ) )
    {
        m_contents.append ( fileLine );
    }
    m_file.close();

    // Vile rapidxml declares input arg as char*, not const char *.
    m_document.parse<0> ( const_cast<char*>(m_contents.c_str()) );  // 0 means default parse flags
}

//----------------------------------------------------------------------------
// Standard "getter".

const xml_document<char> & XmlReader::getDocument() const
{
    return m_document;
}

//============================================================================
// Look through all the "destination" children of the top-level "destinations"
// node and get their descriptions.

void DestinationsReader::generateDestinationDescriptions
(   const set<string> & sectionNames
)
{
    m_sectionNames = &sectionNames;
    xml_node<char> * destinationsChild = m_document.first_node ( "destinations" );
    if ( destinationsChild != 0 )
    {
        for ( xml_node<char> * destination = destinationsChild->first_node ( "destination" );
              destination != 0; destination = destination->next_sibling ( "destination" ) )
        {
            xml_attribute< char > * atlas_id =
                destination->first_attribute ( "atlas_id" );
            if ( atlas_id != 0 )
            {
                m_combinedContent = "";
                getSubTreeContent ( destination );  // pick up all content from sub-tree
                m_descriptions.insert ( pair<int, string> ( atoi ( atlas_id->value() ), m_combinedContent ) );
            }
        }
    }
}

//----------------------------------------------------------------------------
// Recursively gather up all the relevant sections.

void DestinationsReader::getSubTreeContent
(   xml_node< char > * node
)
{
    if ( m_sectionNames->find ( node->name() ) != m_sectionNames->end() )
    {
        xml_node< char > * contentData = node->first_node();
        if ( contentData != 0 )
        {
            m_combinedContent.append ( "<p>" );
            m_combinedContent.append ( contentData->value() );
            m_combinedContent.append ( "</p>" );
        }
    }
    for ( xml_node<char> * child = node->first_node();
          child != 0; child = child->next_sibling() )
    {
        getSubTreeContent ( child );
    }
}

//----------------------------------------------------------------------------
// Find description for given node_id.

string DestinationsReader::getDestinationDescription ( int node_id ) const
{
    map<int, string>::const_iterator descriptionIter =
        m_descriptions.find ( node_id );
    if ( descriptionIter != m_descriptions.end() )
    {
        return descriptionIter->second;
    }
    return "";
}

//============================================================================

void HtmlGenerator::generateFiles ( const char * outputDirName )
{
    // First we have to skip some assumed higher-level nodes.
    const char * taxonomiesString = "taxonomies";
    xml_node<char> * taxonomiesChild = m_taxonomyReader.getDocument().first_node ( taxonomiesString );
    if ( 0 == taxonomiesChild )
    {
        stringstream errorStream;
        errorStream << "Mal-formed taxonomy document: found no first-level \""
                    << taxonomiesString << "\" element";
        throw errorStream.str();
    }

    const char * taxonomyString = "taxonomy";
    xml_node<char> * taxonomyChild = taxonomiesChild->first_node ( taxonomyString );
    if ( 0 == taxonomyChild )
    {
        stringstream errorStream;
        errorStream << "Mal-formed taxonomy document: found no second-level \""
                    << taxonomyString << "\" element";
        throw errorStream.str();
    }

    const char * taxonomy_nameString = "taxonomy_name";
    xml_node<char> * taxonomy_nameChild = taxonomyChild->first_node ( taxonomy_nameString );
    if ( 0 == taxonomy_nameChild )
    {
        stringstream errorStream;
        errorStream << "Mal-formed taxonomy document: found no third-level \""
                    << taxonomy_nameString << "\" element";
        throw errorStream.str();
    }

    m_outputDirectory = outputDirName;
    m_outputDirectory.append ( "/" );
    generateFilesForTree ( 0, taxonomy_nameChild->next_sibling() );
}

//----------------------------------------------------------------------------
// Recursive descent.

void HtmlGenerator::generateFilesForTree
(   xml_node<char> * parent,
    xml_node<char> * node
) const
{
    generateFile ( parent, node );
    for ( xml_node<char> * child = node->first_node ( "node" );
          child != 0; child = child->next_sibling ( "node" ) )
    {
        generateFilesForTree ( node, child );
    }
}

//----------------------------------------------------------------------------
// Create HTML file according to template.

void HtmlGenerator::generateFile
(   xml_node<char> * parent,
    xml_node<char> * node
) const
{
    // Usable node?
    xml_attribute< char > * atlas_node_id =
        node->first_attribute ( "atlas_node_id" );
    if ( 0 == atlas_node_id )    // not a usable node
    {
        return;
    }

    xml_node< char > * node_name = node->first_node ( "node_name" );
    if ( 0 == node_name )   // not a usable node
    {
        return;
    }

    // Construct filename and try to open it for write.
    string htmlFileName ( makeHtmlFileName ( atlas_node_id ) );
    string htmlFilePath ( m_outputDirectory );
    htmlFilePath.append ( htmlFileName );
    ofstream htmlFile ( htmlFilePath.c_str(), ios::out );
    if ( ! htmlFile.is_open() )
    {
        stringstream errorStream;
        errorStream << "Failed to open html file " << htmlFilePath << " for writing";
        throw errorStream.str();
    }

    // Write template+substitutions.
    htmlFile << m_template->getPart1() << node_name->value() << m_template->getPart2();

    if ( parent != 0 )
    {
        xml_attribute< char > * parent_atlas_node_id =
            parent->first_attribute ( "atlas_node_id" );
        xml_node< char > * parent_node_name = parent->first_node ( "node_name" );
        if ( parent_atlas_node_id != 0 && parent_node_name != 0 )
        {
            htmlFile << "<p>Up to <a href=\"" << makeHtmlFileName ( parent_atlas_node_id ) << "\">"
                     << parent_node_name->value() << "</a></p>";
        }
    }

    for ( xml_node<char> * child = node->first_node ( "node" );
          child != 0; child = child->next_sibling ( "node" ) )
    {
        xml_attribute< char > * child_atlas_node_id =
            child->first_attribute ( "atlas_node_id" );
        xml_node< char > * child_node_name = child->first_node ( "node_name" );
        if ( child_atlas_node_id != 0 && child_node_name != 0 )
        {
            htmlFile << "<p><a href=\"" << makeHtmlFileName ( child_atlas_node_id ) << "\">"
                     << child_node_name->value() << "</a></p>";
        }
    }

    htmlFile << m_template->getPart3() << node_name->value() << m_template->getPart4()
             << m_destinationsReader.getDestinationDescription
                    ( atoi ( atlas_node_id->value() ) )
             << m_template->getPart5();

    // Done.
    htmlFile.close();
}

//----------------------------------------------------------------------------
// Build "lp_<nodeid>.html".

string HtmlGenerator::makeHtmlFileName ( xml_attribute< char > * node_id ) const
{
    string htmlFileName ( "lp_" );
    htmlFileName.append ( node_id->value() );
    htmlFileName.append ( ".html" );
    return htmlFileName;
}

//============================================================================

HtmlTemplate * HtmlTemplate::createHtmlTemplate()
{
    static HtmlTemplate * singleton = 0;
    if ( singleton == 0 )
    {
        singleton = new HtmlTemplate;
    }
    return singleton;
}

//----------------------------------------------------------------------------
// I should put the template parts in an external file(s) and read them
// (just once of course).
// I could reduce the number of parts by having substitution points.

HtmlTemplate::HtmlTemplate()
{
    // Oh the joys of inline C++ string constants...

    m_part1 = "<!DOCTYPE html>\n\
<html>\n\
  <head>\n\
    <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n\
    <title>Lonely Planet</title>\n\
    <link href=\"static/all.css\" media=\"screen\" rel=\"stylesheet\" type=\"text/css\">\n\
  </head>\n\
\n\
  <body>\n\
    <div id=\"container\">\n\
      <div id=\"header\">\n\
        <div id=\"logo\"></div>\n\
        <h1>Lonely Planet: ";

    // {DESTINATION NAME}

    m_part2 = "</h1>\n\
      </div>\n\
\n\
      <div id=\"wrapper\">\n\
        <div id=\"sidebar\">\n\
          <div class=\"block\">\n\
            <h3>Navigation</h3>\n\
            <div class=\"content\">\n\
              <div class=\"inner\">\n";

    // HIERARCHY NAVIGATION GOES HERE

    m_part3 = "\n\
              </div>\n\
            </div>\n\
          </div>\n\
        </div>\n\
\n\
        <div id=\"main\">\n\
          <div class=\"block\">\n\
            <div class=\"secondary-navigation\">\n\
              <ul>\n\
                <li class=\"first\"><a href=\"#\">";

    // {DESTINATION NAME}

    m_part4 = "</a></li>\n\
              </ul>\n\
              <div class=\"clear\"></div>\n\
            </div>\n\
            <div class=\"content\">\n\
              <div class=\"inner\">\n";

    // CONTENT GOES HERE

    m_part5 = "\n\
              </div>\n\
            </div>\n\
          </div>\n\
        </div>\n\
      </div>\n\
    </div>\n\
  </body>\n\
</html>\n";
}
