// Compile, link and run. Simples.
// Has no external library dependencies apart from STL.
//
// Uses RapidXml for XML parsing. This was a fairly arbitrary choice.
//
// Run as:
//
// lonely_planet_test <taxonomy-xml-file> <destinations-xml-file>
//                    <output-directory> [ <section-names> ]
// where <section-names> defaults to "overview".
//
// Creates <output-directory> if necessary.
//
// TODO: read template from external file.

#ifdef WIN32
// For _mkdir()
#include <direct.h>
#define MKDIR _mkdir
#else
#define MKDIR mkdir
#endif

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>

#include <rapidxml.hpp>
#include <rapidxml_print.hpp>

using namespace std;
using namespace rapidxml;

// Classes:
//
//  XmlReader: reads a given XML file and uses RapidXml to parse it (once).
//  XmlReader TODO:
//  TODO: (1) make XmlReader abstract and/or make its constructor protected.
//  TODO: (2) destructor should really close m_file if not already closed.
//
//  TaxonomyReader: vanilla specialisation of XmlReader, used by HtmlGenerator
//  to extract from the XML a doubly-linked tree of destinations (well,
//  RapidXml does that for us).
//  TaxonomyReader TODO:
//  TODO: (1) move the tree-traversal from HtmlGenerator::generateFilesForTree
//  TODO: into a TaxonomyReader method, since HtmlGenerator has no business
//  TODO: making assumptions about the form of the XML tree.
//  TODO: (2) Similarly move the level-skipping into TaxonomyReader; also
//  TODO: factorise it into a private method called N times.
//
//  DestinationsReader: specialisation of XmlReader which generates map of
//  destination-to-description.
//  DestinationsReader TODO:
//  DONE: (1) allow nicely for distinguishing multiple content sections in
//  DONE: description.
//
//  HtmlGenerator: generates the HTML files (who would have guessed?) by
//  descending the tree held in TaxonomyReader and correlating the node-ids
//  with the data mapped in DestinationsReader.
//  HtmlGenerator TODO:
//  TODO: (1) copy other necessary files e.g. stylesheet.
//  TODO: (2) destructor should if necessary close currently-open html file
//  TODO: stream (which hence should be a member).
//  TODO: (3) consider generating links on each page to entire ancestor line
//  TODO: of destinations rather than just immediate parent.
//  TODO: (4) consider generating index page which displays entire destination
//  TODO: hierarchy (possibly collapsible).
//  TODO: (5) see also TaxonomyReader TODOs.
//
//  HtmlTemplate: singleton class to hold the HTML template strings.
//  HtmlTemplate TODO:
//  TODO: (1) read from template file instead of having it inline (yuk). Need
//  TODO: to work out how to do the interpolation/substitution, though.
//
//  Main program:
//  Main program TODO:
//  TODO: (1) improve argument-handling: add flags.
//  TODO: (2) plausibly allow handing in of format for generated file names
//  TODO: (rather than hard-coding "lp_<node-id>.html").
//  TODO: (3) cope with multiple tasks in one invocation.
//  TODO: (4) investigate scalability.
//  TODO: (5) investigate robustness, for example what happens if the
//  TODO: recursive descent runs out of stack? (Well we know what happens, but
//  TODO: how best to cope?)
//  TODO: (6) consider generalisation. This is currently stand-alone and
//  TODO: specialised, but the parse-some-xml-files-and-build-some-html could
//  TODO: be useful in other contexts.
//
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
        void getDestinationDescription
        (   int node_id,
            map< string, string > & description
        ) const;

    private:
        void getSubTreeContent ( xml_node< char > * node );

        const set<string> * m_sectionNames;
        map< int, map<string, string> > m_descriptions;
        map< string, string> m_combinedContents;
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
        void createDirectoryRecursively ( const string & directoryName ) const;
        void createDirectory ( const string & directoryName ) const;
        void generateFilesForTree ( xml_node<char> * node ) const;
        void generateFile ( xml_node<char> * node ) const;
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
        cerr << "Error: (" << argv[0]
             << ") needs <taxonomy-xml-file> <destinations-xml-file> "
             << "<output-directory> [ <section-names> ]" << endl;
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
        // RapidXml works, we need to hang on to the file content strings as
        // well as the generated XML tree (because the tree points directly
        // back into the parsed text rather than making its own string
        // copies).
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
        errorStream << "Failed to open " << fileSignifier << " file "
                    << fileName << " for reading";
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
    // 0 means default parse flags
    m_document.parse<0> ( const_cast<char*>(m_contents.c_str()) );

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
    xml_node<char> * destinationsChild = m_document.first_node (
        "destinations" );
    if ( destinationsChild != 0 )
    {
        for ( xml_node<char> * destination = destinationsChild->first_node (
                  "destination" );
              destination != 0;
              destination = destination->next_sibling ( "destination" ) )
        {
            xml_attribute< char > * atlas_id =
                destination->first_attribute ( "atlas_id" );
            if ( atlas_id != 0 )
            {
                for ( set<string>::const_iterator iter = m_sectionNames->begin();
                      iter != m_sectionNames->end(); ++iter )
                {
                    m_combinedContents[*iter] = "";
                }
                // Pick up all content from sub-tree.
                getSubTreeContent ( destination );
                for ( set<string>::const_iterator iter = m_sectionNames->begin();
                      iter != m_sectionNames->end(); ++iter )
                m_descriptions.insert ( pair< int, map< string, string > > (
                    atoi ( atlas_id->value() ), m_combinedContents ) );
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
            string & combinedContent = m_combinedContents[node->name()];
            combinedContent.append ( "<p>" );
            combinedContent.append ( contentData->value() );
            combinedContent.append ( "</p>" );
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

void DestinationsReader::getDestinationDescription
(   int node_id,
    map< string, string > & description
) const
{
    map< int, map< string, string > >::const_iterator descriptionIter =
        m_descriptions.find ( node_id );
    if ( descriptionIter != m_descriptions.end() )
    {
        description = descriptionIter->second;
    }
}

//============================================================================

void HtmlGenerator::generateFiles ( const char * outputDirName )
{
    // First we have to skip some assumed higher-level nodes.
    const char * taxonomiesString = "taxonomies";
    xml_node<char> * taxonomiesChild =
        m_taxonomyReader.getDocument().first_node ( taxonomiesString );
    if ( 0 == taxonomiesChild )
    {
        stringstream errorStream;
        errorStream << "Mal-formed taxonomy document: found no first-level \""
                    << taxonomiesString << "\" element";
        throw errorStream.str();
    }

    const char * taxonomyString = "taxonomy";
    xml_node<char> * taxonomyChild = taxonomiesChild->first_node (
        taxonomyString );
    if ( 0 == taxonomyChild )
    {
        stringstream errorStream;
        errorStream << "Mal-formed taxonomy document: found no second-level \""
                    << taxonomyString << "\" element";
        throw errorStream.str();
    }

#if 0
    const char * taxonomy_nameString = "taxonomy_name";
    xml_node<char> * taxonomy_nameChild = taxonomyChild->first_node (
        taxonomy_nameString );
    if ( 0 == taxonomy_nameChild )
    {
        stringstream errorStream;
        errorStream << "Mal-formed taxonomy document: found no third-level \""
                    << taxonomy_nameString << "\" element";
        throw errorStream.str();
    }
#endif

    // Fudge a root node for "World".

    // A usable node has an attribute "atlas_node_id" and a child_node
    // identified as "node_name".
    // Its children are all the child_nodes identified as "node".
    // So we start with this:

    //  <taxonomies>
    //    <taxonomy>
    //      <taxonomy_name>World</taxonomy_name>
    //      <node atlas_node_id = "355064" ethyl_content_object_id="82534" geo_id = "355064">
    //        <node_name>Africa</node_name>
    //        <node atlas_node_id = "355611" ethyl_content_object_id="3210" geo_id = "355611">
    //          <node_name>South Africa</node_name>
    //          <node atlas_node_id = "355612" ethyl_content_object_id="35474" geo_id = "355612">
    //            <node_name>Cape Town</node_name>
    //            <node atlas_node_id = "355613" ethyl_content_object_id="" geo_id = "355613">
    //              <node_name>Table Mountain National Park</node_name>
    //            </node>
    //          </node>

    // but we want to end up with this:

    // ...
    //
    //    <node atlas_node_id = "0" old_name="taxonomy">
    //      <node_name>World</node_name>
    //
    //      <ignored_taxonomy_name>World</ignored_taxonomy_name>
    //      <node atlas_node_id = "355064" ethyl_content_object_id="82534" geo_id = "355064">
    //        <node_name>Africa</node_name>
    //        <node atlas_node_id = "355611" ethyl_content_object_id="3210" geo_id = "355611">
    //          <node_name>South Africa</node_name>
    //          <node atlas_node_id = "355612" ethyl_content_object_id="35474" geo_id = "355612">
    //            <node_name>Cape Town</node_name>
    //            <node atlas_node_id = "355613" ethyl_content_object_id="" geo_id = "355613">
    //              <node_name>Table Mountain National Park</node_name>
    //            </node>
    //          </node>

    // Modify <taxonomy> to have an atlas_node_id=1 attribute.
    xml_attribute<char> * atlasNodeIdAttr = new xml_attribute<char>;
    string atlasNodeIdAttrName ( "atlas_node_id" );
    atlasNodeIdAttr->name ( atlasNodeIdAttrName.c_str() );
    string atlasNodeIdAttrValue ( "1" );
    atlasNodeIdAttr->value ( atlasNodeIdAttrValue.c_str() );
    taxonomyChild->append_attribute ( atlasNodeIdAttr );

    // Give <taxonomy> a child data node <node_name>.
    xml_node<char> * nodeNameNode = new xml_node<char> ( node_data );
    string nodeNameNodeName ( "node_name" );
    nodeNameNode->name ( nodeNameNodeName.c_str() );
    string nodeNameNodeValue ( "World" );
    nodeNameNode->value ( nodeNameNodeValue.c_str() );
    taxonomyChild->append_node ( nodeNameNode );

    // Rename <taxonomy> as <node>.
    string taxonomyName ( "node" );
    taxonomyChild->name ( taxonomyName.c_str() );

    // Generate hierarchy.
    createDirectory ( outputDirName );
    m_outputDirectory = outputDirName;
    m_outputDirectory.append ( "/" );
    //generateFilesForTree ( 0, taxonomy_nameChild->next_sibling() );
    generateFilesForTree ( taxonomyChild );
}

//----------------------------------------------------------------------------
// Given a/b/c/d:
// recursively call with a/b/c
// recursively call with a/b
// recursively call with a
// create a
// create a/b
// create a/b/c
// create a/b/c/d.

void HtmlGenerator::createDirectoryRecursively
(   const string & directoryName
) const
{
    size_t lastSeparatorIndex = directoryName.find_last_of ( "\\/" );
    if ( lastSeparatorIndex != string::npos )
    {
        createDirectoryRecursively ( directoryName.substr ( 0, lastSeparatorIndex ) );
    }
    // Blithely ignoring errors for now since we will eventually try to
    // create files and that will indicate any mkdir failure implicitly.
    // An error could merely indicate that the directory already exists.
    MKDIR ( directoryName.c_str() );
}

//----------------------------------------------------------------------------
// Given a/b/c/d:
// create a
// create a/b
// create a/b/c
// create a/b/c/d.

void HtmlGenerator::createDirectory
(   const string & directoryName
) const
{
    size_t start = 0;
    for(;;)
    {
        size_t firstSeparatorIndex = directoryName.find_first_of ( "\\/", start );
        string dirName = directoryName.substr ( 0, firstSeparatorIndex );
        // Blithely ignoring errors for now since we will eventually try to
        // create files and that will indicate any mkdir failure implicitly.
        // An error could merely indicate that the directory already exists.
        MKDIR ( dirName.c_str() );
        if ( firstSeparatorIndex == string::npos )
        {
            break;
        }
        start = firstSeparatorIndex+1;
    }
}

//----------------------------------------------------------------------------
// Recursive descent.

void HtmlGenerator::generateFilesForTree ( xml_node<char> * node ) const
{
    generateFile ( node );
    for ( xml_node<char> * child = node->first_node ( "node" );
          child != 0; child = child->next_sibling ( "node" ) )
    {
        generateFilesForTree ( child );
    }
}

//----------------------------------------------------------------------------
// Create HTML file according to template.
// A usable node has an attribute "atlas_node_id" and a child_node identified
// as "node_name".
// Its children are all the child_nodes identified as "node".

void HtmlGenerator::generateFile ( xml_node<char> * node ) const
{
#if 0
    cout << "HtmlGenerator::generateFile: node " << node << " has value " << node->value() << endl;
    for (xml_attribute<> *attr = node->first_attribute();
         attr; attr = attr->next_attribute())
    {
        cout << "    Attribute " << attr->name() << " ";
        cout << "with value " << attr->value() << endl;
    }
    for ( xml_node<char> * child = node->first_node ();
          child != 0; child = child->next_sibling () )
    {
        cout << "    Child node " << child << " with name "<< child->name() << " and value " << child->value() << endl;
    }
#endif

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
        errorStream << "Failed to open html file " << htmlFilePath
                    << " for writing";
        throw errorStream.str();
    }

    // Write template+substitutions.
    htmlFile << m_template->getPart1() << node_name->value()
             << m_template->getPart2();

    vector< xml_node<char>* > ancestors;
    for ( xml_node<char>* parent = node->parent(); parent != 0; parent = parent->parent() )
    {
        ancestors.push_back ( parent );
    }
    for ( vector< xml_node<char>* >::reverse_iterator riter = ancestors.rbegin();
          riter != ancestors.rend(); ++riter )
    {
        xml_attribute< char > * parent_atlas_node_id =
            (*riter)->first_attribute ( "atlas_node_id" );
        xml_node< char > * parent_node_name =
            (*riter)->first_node ( "node_name" );
        if ( parent_atlas_node_id != 0 && parent_node_name != 0 )
        {
            htmlFile << "<p>Up to <a href=\""
                     << makeHtmlFileName ( parent_atlas_node_id ) << "\">"
                     << parent_node_name->value() << "</a></p>";
        }
    }

    for ( xml_node<char> * child = node->first_node ( "node" );
          child != 0; child = child->next_sibling ( "node" ) )
    {
        xml_attribute< char > * child_atlas_node_id =
            child->first_attribute ( "atlas_node_id" );
        xml_node< char > * child_node_name =
            child->first_node ( "node_name" );
        if ( child_atlas_node_id != 0 && child_node_name != 0 )
        {
            htmlFile << "<p><a href=\""
                     << makeHtmlFileName ( child_atlas_node_id ) << "\">"
                     << child_node_name->value() << "</a></p>";
        }
    }

    htmlFile << m_template->getPart3() << node_name->value()
             << m_template->getPart4();
    map< string, string > description;
    m_destinationsReader.getDestinationDescription ( atoi ( atlas_node_id->value() ), description );
    for ( map< string, string >::const_iterator iter = description.begin();
          iter != description.end(); ++iter )
    {
        string heading ( iter->first );
        heading[0] = toupper ( heading[0] );
        htmlFile << "<h3>" << heading << "</h3>" << iter->second;
    }
    htmlFile << m_template->getPart5();

    // Done.
    htmlFile.close();
}

//----------------------------------------------------------------------------
// Build "lp_<nodeid>.html".

string HtmlGenerator::makeHtmlFileName
(   xml_attribute< char > * node_id
) const
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
