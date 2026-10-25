# Resource Inspector

Open Resource Inspector from Applications to browse the desktop resource tree.
Its Endpoint field contains the private host directory inherited from the
running desktop. Copy endpoint copies that directory for a local command client.
The desktop creates a private session directory when
`STOMME_RESOURCE_DIRECTORY` is unset; an explicit absolute directory selects
another endpoint location.

Enter a namespace path such as `/settings/ui/theme` and press Inspect. The
response contains values, schema, availability, permissions and revision
metadata. The proposal editor accepts typed JSON. Apply value submits a setting
at its displayed revision; Invoke action submits typed action arguments. Watches
mark changed observations for refresh. Job IDs, cancellation and retained request
receipts remain visible in the inspection view. Native confirmation requirements
also apply to inspector requests.

The product installs `system/stomme-resources` alongside its desktop executable.
For a build using `BUILD=build/sv3`:

```sh
build/sv3/runtime/system/stomme-resources --directory /absolute/private/session list /
build/sv3/runtime/system/stomme-resources --directory /absolute/private/session describe /settings/ui/theme
build/sv3/runtime/system/stomme-resources --directory /absolute/private/session read /settings/ui/theme
build/sv3/runtime/system/stomme-resources --directory /absolute/private/session set /settings/ui/theme enum dark
```

The directory argument is a host filesystem path. `/settings/ui/theme` belongs
to the desktop resource namespace. Mutation output is an initial receipt or job;
query `job JOB_ID` for completion and `receipt REQUEST_ID` for reconciliation.
Clients do not automatically retry a mutation whose reply was lost.

The application is built from Stomme's selected source and a product manifest.
It also builds through `apps/resource-inspector/Makefile` against an exported SDK;
see [independent application builds](../architecture/application-builds.md).
