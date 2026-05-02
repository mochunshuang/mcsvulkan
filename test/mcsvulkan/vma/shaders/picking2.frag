#version 450
layout(location=0)out uvec2 outID;
layout(location=0)in flat uint fragObjectId;

void main(){
    outID=uvec2(fragObjectId,gl_PrimitiveID);
}