# Run as:
#
# lonely_planet_test <taxonomy-xml-file> <destinations-xml-file> <output-directory> [ <section-names> ]
# where <section-names> defaults to "overview".
#
# Does *not* create <output-directory>.
#
# TODO: read template from external file.
# TODO: identify different content sections e.g.
# Overview
# <overview-stuff>
# Money
# <money-stuff>

require "rexml/document"
require "set"

#============================================================================
# Look through all the "destination" children of the top-level "destinations"
# node and get their descriptions.

class DestinationsReader
    def initialize ( sectionNames )
        @sectionNames = sectionNames
        @descriptions = Hash.new
        @level = 0
    end

    def generateDestinationDescriptions ( destinationsNode )
        destinationsNode.elements.each ( "destination" ) do |destination|
            atlas_id = destination.attributes["atlas_id"]
            if atlas_id != 0 then
                @combinedContent = ""
                getSubTreeContent ( destination )   # pick up all content from sub-tree
                @descriptions[atlas_id] = @combinedContent
            end
        end
    end

    # Recursively gather up all the relevant sections.
    def getSubTreeContent ( node )
        @level += 1
        if @sectionNames.include?(node.name) then
            node.cdatas.each { |cdata|
                @combinedContent << "<p>#{cdata}</p>"
            }
        end
        node.elements.each { |child|
            getSubTreeContent ( child )
        }
        @level -= 1
    end

    # Find description for given node_id.
    def getDestinationDescription ( node_id )
        value = @descriptions[node_id.value]
        return value ? value : ""
    end

end

#============================================================================

class HtmlGenerator

    def initialize ( taxonomyReader, destinationsReader )
        @taxonomyReader = taxonomyReader
        @destinationsReader = destinationsReader
        @template = HtmlTemplate.new
    end

    # Build "lp_<nodeid>.html".
    def makeHtmlFileName ( node_id )
        "lp_#{node_id}.html"
    end

    # Create HTML file according to template.
    def generateFile ( parent, node )
        # Usable node?
        atlas_node_id = node.attribute ( "atlas_node_id" )
        if ! atlas_node_id then  # not a usable node
            return nil  # prevent procedure-as-function-(ab)use
        end

        node_name = node.elements["node_name"].text
        if ! node_name then  # not a usable node
            return nil  # prevent procedure-as-function-(ab)use
        end

        # Construct filename and try to open it for write.
        htmlFilePath = "#{@outputDirectory}#{makeHtmlFileName(atlas_node_id)}"
        begin
            File.open( htmlFilePath, "w" ) do | outFile |
                # Write template+substitutions.
                outFile.puts "#{@template.part1}#{node_name}#{@template.part2}"

                if parent then
                    parent_atlas_node_id = parent.attributes["atlas_node_id"]
                    parent_node_name = parent.elements["node_name"]
                    if parent_atlas_node_id && parent_node_name then
                        outFile.puts "<p>Up to <a href=\"#{makeHtmlFileName(parent_atlas_node_id)}\">"
                        outFile.puts "#{parent_node_name.text}</a></p>"
                    end
                end

                node.elements.each() {|child|
                    child_atlas_node_id = child.attributes["atlas_node_id"]
                    child_node_name = child.elements["node_name"]
                    if child_atlas_node_id && child_node_name then
                        outFile.puts "<p><a href=\"#{makeHtmlFileName(child_atlas_node_id)}\">"
                        outFile.puts "#{child_node_name.text}</a></p>"
                    end
                }

                outFile.puts "#{@template.part3}#{node_name}#{@template.part4}"
                outFile.puts "#{@destinationsReader.getDestinationDescription(atlas_node_id)}"
                outFile.puts @template.part5
            end
        rescue
            raise "Failed to write html file #{htmlFilePath}"
        end
    end

    # Recursive descent.
    def generateFilesForTree ( parent, node )
        generateFile( parent, node )
        node.elements.each { |child|
            generateFilesForTree( node, child )
        }
        return nil  # prevent procedure-as-function-(ab)use
    end

    def generateFiles ( outputDirName )
        @outputDirectory = "#{outputDirName}/"
        generateFilesForTree( nil, @taxonomyReader )
        return nil  # prevent procedure-as-function-(ab)use
    end

end

class HtmlTemplate
    attr_reader :part1, :part2, :part3, :part4, :part5

    def initialize
        if @part1
            return
        end

        # I should read the template parts in an external file and do clever
        # things with substitutions.
        @part1 = <<-PART1
<!DOCTYPE html>
<html>
  <head>
    <meta http-equiv="content-type" content="text/html; charset=UTF-8">
    <title>Lonely Planet</title>
    <link href="static/all.css" media="screen" rel="stylesheet" type="text/css">
  </head>

  <body>
    <div id="container">
      <div id="header">
        <div id="logo"></div>
        <h1>Lonely Planet:
PART1

        # {DESTINATION_NAME}

        @part2 = <<-PART2
</h1>
      </div>

      <div id="wrapper">
        <div id="sidebar">
          <div class="block">
            <h3>Navigation</h3>
            <div class="content">
              <div class="inner">
PART2

        # HIERARCHY NAVIGATION GOES HERE

        @part3 = <<-PART3
              </div>
            </div>
          </div>
        </div>

        <div id="main">
          <div class="block">
            <div class="secondary-navigation">
              <ul>
                <li class="first"><a href="#">
PART3

        # {DESTINATION_NAME}

        @part4 = <<-PART4
</a></li>
              </ul>
              <div class="clear"></div>
            </div>
            <div class="content">
              <div class="inner">
PART4

        # CONTENT GOES HERE

        @part5 = <<-PART5
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </body>
</html>
PART5
    return nil  # prevent procedure-as-function-(ab)use
    end

end

#============================================================================
# "main"

# Check arguments.
if ARGV.size < 3 then
    warn "Error: ($0) needs <taxonomy-xml-file> <destinations-xml-file> <output-directory> [ <section-names> ]"
    return 1
end

# Get optional section names. If none supplied, use "overview".
# Would it be more Rubyesque to wrap this into the actual call argument?
sectionNames = Set.new

# standard Ruby idiom? [n..-1] is "inclusive to one less than the end".
ARGV[3..-1].each do | arg |
    sectionNames.add arg
end
if sectionNames.empty? then
    sectionNames.add "overview"
end

begin
    # Slurp and parse entire files.
    # Could condense lines more but that's getting a bit cumbersome to read.
    begin
        taxonomyData = REXML::Document.new ( File.read ( ARGV[0] ) )
        # Step down through taxonomies taxonomy taxonomy_name
        # (although it would be kinder to do each step explicitly so we can
        # raise different exceptions at each level).
    rescue
        raise "Failed to read taxonomy file #{ARGV[0]}"
    end
    taxonomyNameSibling = taxonomyData.elements.each("taxonomies"){}[0].elements[1].elements[2]
    if ! taxonomyNameSibling then
        raise "Failed to find taxonomies.taxonomy.taxonomy_name sibling element"
    end

    begin
        destinationsData = REXML::Document.new ( File.read ( ARGV[1] ) )
    rescue
        raise "Failed to read destinations file #{ARGV[1]}"
    end
    destinationsNode = destinationsData.elements.each("destinations"){}[0]
    if ! destinationsNode then
        raise "Failed to find destinations element"
    end

    destinationsReader = DestinationsReader.new ( sectionNames )
    destinationsReader.generateDestinationDescriptions( destinationsNode )
    htmlGenerator = HtmlGenerator.new( taxonomyNameSibling, destinationsReader )
    htmlGenerator.generateFiles( ARGV[2] )
rescue => exception
    warn "Caught exception: #{exception.message}"
    raise
end
