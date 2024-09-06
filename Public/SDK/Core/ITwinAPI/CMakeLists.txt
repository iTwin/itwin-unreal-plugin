
add_library ( ITwinAPI OBJECT
	ITwinEnvironment.h
	ITwinTypes.h
	ITwinWebServices.cpp
	ITwinWebServices.h
	ITwinWebServicesObserver.cpp
	ITwinWebServicesObserver.h
)
target_compile_features ( ITwinAPI PRIVATE ${DefaultCXXSTD} )
target_link_libraries( ITwinAPI PRIVATE 
					   Network
					   Tools
					   fmt::fmt-header-only
					  )
set_target_properties ( ITwinAPI PROPERTIES FOLDER "SDK/Core" ) 
