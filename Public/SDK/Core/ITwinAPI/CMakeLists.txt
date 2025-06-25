
add_library ( ITwinAPI OBJECT
	ITwinAuthInfo.h
	ITwinAuthManager.cpp
	ITwinAuthManager.h
	ITwinAuthObserver.h
	ITwinAuthStatus.h
	ITwinEnvironment.h
	ITwinMaterial.cpp
	ITwinMaterial.h
	ITwinMaterial.inl
	ITwinMaterialPrediction.h
	ITwinMatMLPredictionEnums.h
	ITwinScene.h
	ITwinRequestDump.h
	ITwinRequestTypes.h
	ITwinTypes.h
	ITwinWebServices.cpp
	ITwinWebServices.h
	ITwinWebServicesObserver.cpp
	ITwinWebServicesObserver.h
)
target_compile_features ( ITwinAPI PRIVATE ${DefaultCXXSTD} )
if ( MSVC )
	set_source_files_properties ( ITwinWebServices.cpp PROPERTIES COMPILE_FLAGS " /bigobj" )
	target_compile_options(ITwinAPI PRIVATE /utf-8) # Needed for "fmt" lib.
endif ()
target_link_libraries( ITwinAPI PRIVATE 
					   Network
					   Tools
					   Json
					  )
set_target_properties ( ITwinAPI PROPERTIES FOLDER "SDK/Core" ) 
