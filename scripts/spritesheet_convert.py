# Convert XML sprites in to a LUA table.
import xml.dom.minidom as minidom
import sys

def main(argv):
    doc = minidom.parse(argv[1])
    subtextures = []
    for subtexture in doc.getElementsByTagName("SubTexture"): 
        subtextures.append([subtexture.getAttribute(k) for k in ["name", "x", "y", "width", "height"]])
    print("return {")
    for [name, x, y, width, height] in subtextures:
        print("{{ name = \"{}\", x = {}, y = {}, width = {}, height = {} }},".format(name, x, y, width, height).strip())
    print("}")
    pass

if __name__ == '__main__':
    main(sys.argv)